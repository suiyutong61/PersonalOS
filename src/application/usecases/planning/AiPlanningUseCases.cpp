#include "application/usecases/planning/AiPlanningUseCases.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "application/audit/Audit.h"
#include "application/foundation/ContractValidator.h"
#include "application/foundation/SupportAssessor.h"
#include "application/usecases/domain/DecisionValidator.h"
#include "application/usecases/domain/DomainRegistry.h"
#include "application/usecases/domain/StateContextBuilder.h"
#include "application/usecases/mel/MelUseCases.h"
#include "application/usecases/route/RouteUseCases.h"

namespace PersonOS::Application {

namespace {

constexpr int kKeySubQuestions = 2;   // 范围依据 + 方法/可行性依据（确定性初版）

// 契约字段说明（写入提示词；AI 只能输出这些字段，不得虚构引用）
const char *routeContractHint = R"TEXT(
输出 JSON（route_proposal_v1）：{"goal_uid":"<保持原值>","stages":[{"title":"阶段名"}],
"rationale":"拆分依据（引用用户目标/知识库/现实约束）","evidence_summary":"所用知识来源摘要",
"assumptions":{},"source_mode":"grounded|partially_grounded|ungrounded"}
)TEXT";

const char *melContractHint = R"TEXT(
输出 JSON（mel_proposal_v1）：{"title":"本轮标题","tasks":[{"title":"任务名",
"planned_effort_min":整数分钟,"required":true}],"rationale":"容量与依据说明",
"capacity_min":整数,"reserve_min":整数,"period_days":数字,"goal_uid":"<保持原值>",
"source_mode":"grounded|partially_grounded|ungrounded"}
)TEXT";

const char *methodContractHint = R"TEXT(
输出 JSON（method_suggestions_v1）：{"suggestions":[{"task_uid":"<保持原值>",
"method_version_uid":"必须来自候选方法列表","reason":"匹配理由",
"applicability":"适用条件","risks":"风险与不适用情形"}]}
)TEXT";

const char *advisorContractHint = R"TEXT(
输出 JSON（generic_v1）：{"user_text":"面向用户的完整回答（含知识支持程度说明）",
"source_mode":"grounded|partially_grounded|ungrounded"}
)TEXT";

// JSON 字符串转义（用户数据/提示词不得破坏外层 JSON 结构）
std::string jsonEscape(const std::string &value)
{
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value) {
        if (c == '"') {
            out.push_back('\\');
            out.push_back('"');
        } else if (c == '\\') {
            out.push_back('\\');
            out.push_back('\\');
        } else if (c == '\n') {
            out.push_back('\\');
            out.push_back('n');
        } else if (c == '\r') {
            out.push_back('\\');
            out.push_back('r');
        } else if (c == '\t') {
            out.push_back('\\');
            out.push_back('t');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string protocolPrompt(const DomainConfig &config)
{
    return std::string("你是 Personal OS 的规划助手。规则：1) 重要计划必须引用知识库检索结果，"
                       "不得虚构来源；2) 知识支持有限时明确说明，无支持时标记 ungrounded；"
                       "3) 用户是最终决策者，你只生成候选，不直接生效；"
                       "4) 结合用户现实状态（精力/时间/负荷）给出可持续安排，"
                       "不按理论最大时间填满，保留缓冲与休息；"
                       "5) 不把休息或暂停解释为不自律。领域：")
           + config.domainCode + "，工作流：" + [&config]() {
                 std::string workflows;
                 for (const auto &workflow : config.workflows)
                     workflows += workflow + " ";
                 return workflows;
             }();
}

} // namespace

AiPlanningUseCases::AiPlanningUseCases(
    AiGatewayPort &gateway, AiConfigStore &configs, KnowledgeRetrievalPort &retrieval,
    KnowledgeRepository &knowledge, GoalRepository &goals, MelRepository &mels,
    RouteRepository &routes, StateRepository &states,
    DomainManifestRepository &manifests, DecisionStore &decisions, UuidPort &uids,
    const Domain::Clock &clock)
    : m_gateway(gateway), m_configs(configs), m_retrieval(retrieval),
      m_knowledge(knowledge), m_goals(goals),
      m_mels(mels), m_routes(routes), m_states(states), m_manifests(manifests),
      m_decisions(decisions), m_uids(uids), m_clock(clock)
{}

Result<AiPlanningUseCases::CalibrationContext, ApplicationError>
AiPlanningUseCases::calibrate(const std::string &purpose, const std::string &queryText,
                              const std::string &filtersJson, int keySubQuestions)
{
    DomainRegistry registry(m_manifests);
    const auto config = registry.load("learning");
    if (!config)
        return Result<CalibrationContext, ApplicationError>::failure(config.error());

    // 检索（结构化过滤 + FTS 召回；命中与分数落库可追溯）
    RetrievalRequest request;
    request.purpose = purpose;
    request.queryText = queryText;
    request.filtersJson = filtersJson;
    const auto retrieved = m_retrieval.retrieve(request);
    if (!retrieved)
        return Result<CalibrationContext, ApplicationError>::failure(retrieved.error());

    // 知识支持判定（DR-028 确定性初版：以关键子问题覆盖为准；
    // 确定性规则不宣称充分覆盖——AI 评估部分接入后升级）
    SupportAssessmentInput assessmentInput;
    assessmentInput.totalKeySubQuestions = keySubQuestions;
    assessmentInput.coveredSubQuestions =
        retrieved.value().hits.empty() ? 0 : 1;   // 有实质命中 = 部分覆盖
    const auto support = SupportAssessor::assess(assessmentInput);

    // 知识快照（同内容幂等；后续复现不受版本漂移影响）
    QJsonArray versions;
    for (const auto &hit : retrieved.value().hits) {
        // 命中返回条目 uid；版本化引用需当前版本 uid
        std::string versionUid = hit.ownerUid;
        if (const auto item = m_knowledge.findItem(
                Domain::Uid::parse(hit.ownerUid)
                    ? *Domain::Uid::parse(hit.ownerUid)
                    : Domain::Uid{})) {
            if (item->currentVersionUid && !item->currentVersionUid->empty())
                versionUid = *item->currentVersionUid;
        }
        versions.append(QString::fromStdString(versionUid));
    }
    const std::string versionsJson =
        QString::fromUtf8(QJsonDocument(versions).toJson(QJsonDocument::Compact))
            .toStdString();
    const auto snapshot = m_retrieval.snapshot(
        purpose, "[\"" + config.value().manifestVersionUid + "\"]", versionsJson,
        retrieved.value().run.uid.value());
    if (!snapshot)
        return Result<CalibrationContext, ApplicationError>::failure(snapshot.error());

    CalibrationContext context;
    context.config = std::move(config.value());
    context.knowledgeVersionsJson = versionsJson;
    context.knowledgeSummary = retrieved.value().hits.empty()
                                   ? "[]"
                                   : "{\"hits\":"
                                         + std::to_string(retrieved.value().hits.size())
                                         + "}";
    context.snapshotUid = snapshot.value().uid.value();
    context.sourceMode =
        support.level == Domain::KnowledgeSupportLevel::Grounded
            ? Domain::SourceMode::KnowledgeGrounded
            : (support.level == Domain::KnowledgeSupportLevel::PartiallyGrounded
                   ? Domain::SourceMode::PartiallyGrounded
                   : Domain::SourceMode::Ungrounded);
    context.hasMaterialConflict = support.hasMaterialConflict;
    return Result<CalibrationContext, ApplicationError>::success(std::move(context));
}

Result<AiPlanningUseCases::ProposalOutput, ApplicationError> AiPlanningUseCases::runJob(
    const std::string &jobType, const std::string &contractType,
    const std::string &payloadJson, const CalibrationContext &calibration)
{
    const auto config = m_configs.findFirstEnabledConfig();
    if (!config)
        return Result<ProposalOutput, ApplicationError>::failure(
            {ErrorCode::ExternalUnavailable,
             "no enabled provider config (configure and test a model connection first)", {},
             true});

    AiGatewaySubmit submit;
    submit.providerConfigUid = config->uid;
    submit.jobType = jobType;
    submit.contractType = contractType;
    submit.contractVersion = "1";
    submit.requestJson = payloadJson;
    submit.idempotencyKey = jobType + ":" + m_uids.next().value();
    submit.sourceMode = calibration.sourceMode;
    submit.maxAttempts = 3;
    AiContextItem snapshotItem;
    snapshotItem.contextType = "knowledge_snapshot";
    snapshotItem.contextUid = calibration.snapshotUid;
    snapshotItem.versionUid = calibration.config.manifestVersionUid;
    snapshotItem.reason = "知识快照";
    submit.contextItems.push_back(snapshotItem);

    const auto registered = m_gateway.submit(submit);
    if (!registered)
        return Result<ProposalOutput, ApplicationError>::failure(registered.error());
    const auto executed = m_gateway.execute(registered.value().uid);
    if (!executed)
        return Result<ProposalOutput, ApplicationError>::failure(executed.error());
    if (executed.value().status == Domain::AiJobStatus::FailedRetryable)
        return Result<ProposalOutput, ApplicationError>::failure(
            {ErrorCode::ExternalUnavailable, "ai job failed (retryable)", {}, true});
    if (executed.value().status != Domain::AiJobStatus::Completed)
        return Result<ProposalOutput, ApplicationError>::failure(
            {ErrorCode::ExternalUnavailable, "ai job not completed", {}, true});
    if (!executed.value().resultJson)
        return Result<ProposalOutput, ApplicationError>::failure(
            {ErrorCode::Validation, "ai job missing structured result", {}, false});

    const std::string structured = *executed.value().resultJson;
    if (const auto error = ContractValidator::validate(contractType, structured))
        return Result<ProposalOutput, ApplicationError>::failure(
            {ErrorCode::Validation, "contract validation failed: " + *error, {}, false});

    ProposalOutput output;
    output.knowledgeSnapshotUid = calibration.snapshotUid;
    output.sourceMode = calibration.sourceMode;
    output.userText = structured;
    return Result<ProposalOutput, ApplicationError>::success(std::move(output));
}

Result<AiPlanningUseCases::ProposalOutput, ApplicationError>
AiPlanningUseCases::generateRouteProposal(const Domain::Uid &userId,
                                          const Domain::Uid &goalUid)
{
    const auto goal = m_goals.findByUid(goalUid);
    if (!goal)
        return Result<ProposalOutput, ApplicationError>::failure(
            {ErrorCode::NotFound, "goal not found", {}, false});

    auto calibration = calibrate("route_planning",
                                 goal->title + " " + goal->description,
                                 "{\"domain_code\":\"learning\"}", kKeySubQuestions);
    if (!calibration)
        return Result<ProposalOutput, ApplicationError>::failure(calibration.error());

    // 最小必要状态（在调用点构建，保证 userId 真实）
    StateContextBuilder contextBuilder(m_states, m_clock);
    const auto stateContext = contextBuilder.build(userId, m_clock.utcIso());
    calibration.value().stateContextJson =
        stateContext ? stateContext.value() : std::string("{}");

    const std::string instruction =
        jsonEscape(protocolPrompt(calibration.value().config) + std::string(" ")
                   + std::string(routeContractHint));
    const std::string payload =
        "{\"goal_uid\":\"" + goalUid.value() + "\",\"goal_title\":\""
        + jsonEscape(goal->title) + "\",\"goal_description\":\""
        + jsonEscape(goal->description) + "\",\"_instruction\":\"" + instruction
        + "\",\"_state\":" + calibration.value().stateContextJson + ",\"_knowledge\":"
        + calibration.value().knowledgeSummary + "}";

    const auto job = runJob("route_proposal", "route_proposal_v1", payload,
                            calibration.value());
    if (!job)
        return Result<ProposalOutput, ApplicationError>::failure(job.error());

    // 硬约束校验（引用完整性/结构/领域参数）
    DomainRegistry registry(m_manifests);
    const auto config = registry.load("learning");
    DecisionValidator validator(m_goals);
    const auto outcome =
        config ? validator.validate("route_proposal_v1", job.value().userText,
                                    config.value(), userId)
               : DecisionValidator::ValidationOutcome{};
    if (!outcome.ok) {
        std::string errors;
        for (const auto &error : outcome.errors)
            errors += error + ";";
        return Result<ProposalOutput, ApplicationError>::failure(
            {ErrorCode::Validation, "candidate rejected by hard constraints: " + errors, {},
             false});
    }

    // 候选落库（AI 生成的是候选；用户确认后成为当前路线）
    const QJsonObject proposal =
        QJsonDocument::fromJson(QByteArray::fromStdString(job.value().userText)).object();
    RouteUseCases routeUseCases(m_routes, m_goals, m_uids, m_clock);
    RouteUseCases::ProposeInput input;
    input.goalId = goalUid;
    input.rationale = proposal.value(QStringLiteral("rationale")).toString().toStdString();
    input.evidenceSummary =
        proposal.value(QStringLiteral("evidence_summary")).toString().toStdString();
    input.assumptionsJson =
        QString::fromUtf8(QJsonDocument(proposal.value(QStringLiteral("assumptions")).toObject())
                              .toJson(QJsonDocument::Compact))
            .toStdString();
    input.createdBy = "ai";
    int sequence = 0;
    for (const auto &value : proposal.value(QStringLiteral("stages")).toArray()) {
        Domain::RouteStage stage;
        stage.title = value.toObject().value(QStringLiteral("title")).toString().toStdString();
        stage.sequenceNo = sequence++;
        stage.completionRuleJson = std::string("{}");
        input.stages.push_back(std::move(stage));
    }
    const auto proposed = routeUseCases.proposeRoute(input);
    if (!proposed)
        return Result<ProposalOutput, ApplicationError>::failure(proposed.error());

    // 决策记录（依据可追溯；用户确认后由 markDecision 更新）
    Domain::DecisionRecord decision;
    decision.uid = m_uids.next();
    decision.decisionType = "route_proposal";
    decision.aggregateType = "route";
    decision.aggregateUid = proposed.value().route.uid.value();
    decision.inputSnapshotJson =
        "{\"goal\":\"" + goalUid.value() + "\",\"snapshot\":\""
        + calibration.value().snapshotUid + "\"}";
    decision.candidateJson = job.value().userText;
    decision.rationale = input.rationale;
    decision.sourceMode = calibration.value().sourceMode;
    decision.userStatus = Domain::DecisionUserStatus::Pending;
    decision.createdAt = m_clock.utcIso();
    const auto savedDecision = m_decisions.insertDecision(decision);
    if (!savedDecision.ok)
        return Result<ProposalOutput, ApplicationError>::failure(savedDecision.error);

    Audit::record({"ai", {}, "route.candidate_generated", "route",
                   proposed.value().route.uid.value(), "{}"});

    ProposalOutput output = job.value();
    output.aggregateUid = proposed.value().route.uid.value();
    output.decisionUid = decision.uid.value();
    return Result<ProposalOutput, ApplicationError>::success(std::move(output));
}

Result<AiPlanningUseCases::ProposalOutput, ApplicationError>
AiPlanningUseCases::generateMelProposal(const Domain::Uid &userId,
                                        const Domain::Uid &goalUid,
                                        const std::optional<Domain::Uid> &routeVersionUid)
{
    const auto goal = m_goals.findByUid(goalUid);
    if (!goal)
        return Result<ProposalOutput, ApplicationError>::failure(
            {ErrorCode::NotFound, "goal not found", {}, false});

    auto calibration = calibrate("mel_planning", goal->title,
                                 "{\"domain_code\":\"learning\"}", kKeySubQuestions);
    if (!calibration)
        return Result<ProposalOutput, ApplicationError>::failure(calibration.error());

    StateContextBuilder contextBuilder(m_states, m_clock);
    const auto stateContext = contextBuilder.build(userId, m_clock.utcIso());
    calibration.value().stateContextJson =
        stateContext ? stateContext.value() : std::string("{}");

    // 周期来自领域清单参数（公共核心不写死三天）
    double periodDays = 3.0;
    if (const auto spec = calibration.value().config.parameter("mel_period_days"))
        periodDays = spec->defaultValue.value_or(3.0);

    const std::string instruction =
        jsonEscape(protocolPrompt(calibration.value().config) + std::string(" ")
                   + std::string(melContractHint) + " 建议 period_days 为 "
                   + std::to_string(periodDays));
    const std::string payload =
        "{\"goal_uid\":\"" + goalUid.value() + "\",\"_instruction\":\""
        + instruction + "\",\"_state\":" + calibration.value().stateContextJson
        + ",\"_knowledge\":" + calibration.value().knowledgeSummary + "}";

    const auto job = runJob("mel_proposal", "mel_proposal_v1", payload,
                            calibration.value());
    if (!job)
        return Result<ProposalOutput, ApplicationError>::failure(job.error());

    DomainRegistry registry(m_manifests);
    const auto config = registry.load("learning");
    DecisionValidator validator(m_goals);
    const auto outcome =
        config ? validator.validate("mel_proposal_v1", job.value().userText,
                                    config.value(), userId)
               : DecisionValidator::ValidationOutcome{};
    if (!outcome.ok) {
        std::string errors;
        for (const auto &error : outcome.errors)
            errors += error + ";";
        return Result<ProposalOutput, ApplicationError>::failure(
            {ErrorCode::Validation, "candidate rejected by hard constraints: " + errors, {},
             false});
    }

    const QJsonObject proposal =
        QJsonDocument::fromJson(QByteArray::fromStdString(job.value().userText)).object();

    MelUseCases melUseCases(m_mels, m_uids, m_clock);
    MelUseCases::CreateInput input;
    input.userId = userId;
    input.goalId = goalUid;
    input.routeVersionId = routeVersionUid;
    if (!config)
        return Result<ProposalOutput, ApplicationError>::failure(config.error());
    // manifest version id：从学习清单条目解析（MelUseCases 必填）
    const auto manifestUid = m_manifests.findManifestByCode("learning");
    if (!manifestUid)
        return Result<ProposalOutput, ApplicationError>::failure(
            {ErrorCode::NotFound, "learning manifest missing", {}, false});
    const auto manifestVersion = m_manifests.latestActiveVersion(*manifestUid);
    if (!manifestVersion)
        return Result<ProposalOutput, ApplicationError>::failure(
            {ErrorCode::NotFound, "learning manifest version missing", {}, false});
    const auto parsedManifestVersion = Domain::Uid::parse(manifestVersion->versionUid);
    if (!parsedManifestVersion)
        return Result<ProposalOutput, ApplicationError>::failure(
            {ErrorCode::Validation, "manifest version uid invalid", {}, false});
    input.manifestVersionId = *parsedManifestVersion;

    input.title = proposal.value(QStringLiteral("title")).toString().toStdString();
    input.rationale = proposal.value(QStringLiteral("rationale")).toString().toStdString();
    input.capacityMin = proposal.value(QStringLiteral("capacity_min")).toInt();
    input.reserveMin = proposal.value(QStringLiteral("reserve_min")).toInt();
    input.plannedStartAt = m_clock.utcIso();
    const double days =
        proposal.value(QStringLiteral("period_days")).toDouble(periodDays);
    input.plannedEndAt = m_clock.utcIsoPlusMinutes(static_cast<int>(days * 1440.0));
    input.timezoneId = "Asia/Shanghai";
    input.settlementMode = "deadline";
    input.knowledgeSnapshotUid = calibration.value().snapshotUid;
    for (const auto &value : proposal.value(QStringLiteral("tasks")).toArray()) {
        const QJsonObject taskObject = value.toObject();
        Domain::MelTask task;
        task.title = taskObject.value(QStringLiteral("title")).toString().toStdString();
        task.required = taskObject.value(QStringLiteral("required")).toBool(true);
        task.plannedEffortMin =
            taskObject.value(QStringLiteral("planned_effort_min")).toInt();
        task.completionRuleJson = std::string("{}");
        input.tasks.push_back(std::move(task));
    }
    const auto created = melUseCases.createMelProposal(input);
    if (!created)
        return Result<ProposalOutput, ApplicationError>::failure(created.error());

    // 计划预测（追加式；预测→实际→校准闭环的输入端，DR-018/036）
    {
        Domain::MelPrediction prediction;
        prediction.uid = m_uids.next();
        prediction.melId = created.value().mel.uid;
        prediction.predictedCompletion = 1.0;   // 计划目标的完成预期
        prediction.predictedEffortMin = input.capacityMin;
        prediction.riskLevel = calibration.value().sourceMode
                                       == Domain::SourceMode::Ungrounded
                                   ? "medium"
                                   : "low";
        prediction.basisJson = "{\"source_mode\":\""
                               + std::string(Domain::toString(calibration.value().sourceMode))
                               + "\",\"snapshot_uid\":\""
                               + calibration.value().snapshotUid + "\"}";
        prediction.createdAt = m_clock.utcIso();
        m_mels.insertPrediction(prediction);
    }

    Domain::DecisionRecord decision;
    decision.uid = m_uids.next();
    decision.decisionType = "mel_proposal";
    decision.aggregateType = "mel";
    decision.aggregateUid = created.value().mel.uid.value();
    decision.inputSnapshotJson =
        "{\"goal\":\"" + goalUid.value() + "\",\"snapshot\":\""
        + calibration.value().snapshotUid + "\"}";
    decision.candidateJson = job.value().userText;
    decision.rationale = input.rationale;
    decision.sourceMode = calibration.value().sourceMode;
    decision.userStatus = Domain::DecisionUserStatus::Pending;
    decision.createdAt = m_clock.utcIso();
    const auto savedDecision = m_decisions.insertDecision(decision);
    if (!savedDecision.ok)
        return Result<ProposalOutput, ApplicationError>::failure(savedDecision.error);

    Audit::record({"ai", {}, "mel.candidate_generated", "mel",
                   created.value().mel.uid.value(), "{}"});

    ProposalOutput output = job.value();
    output.aggregateUid = created.value().mel.uid.value();
    output.decisionUid = decision.uid.value();
    return Result<ProposalOutput, ApplicationError>::success(std::move(output));
}

Result<int, ApplicationError> AiPlanningUseCases::generateMethodSuggestions(
    const Domain::Uid &userId, const Domain::Uid &melUid)
{
    Q_UNUSED(userId);
    const auto mel = m_mels.findByUid(melUid);
    if (!mel)
        return Result<int, ApplicationError>::failure(
            {ErrorCode::NotFound, "mel not found", {}, false});
    const auto tasks = m_mels.tasksOf(melUid);
    if (tasks.empty())
        return Result<int, ApplicationError>::failure(
            {ErrorCode::Validation, "mel has no tasks", {}, false});

    std::string queryText;
    for (const auto &task : tasks)
        queryText += task.title + " ";
    auto calibration =
        calibrate("method_suggestion", queryText, "{\"library_type\":\"method\"}",
                  kKeySubQuestions);
    if (!calibration)
        return Result<int, ApplicationError>::failure(calibration.error());

    // 候选方法集合（AI 只能引用召回到的 method 版本 uid）
    std::vector<std::string> candidateMethodUids;
    {
        QJsonDocument document =
            QJsonDocument::fromJson(QByteArray::fromStdString(
                calibration.value().knowledgeVersionsJson));
        for (const auto &value : document.array())
            candidateMethodUids.push_back(value.toString().toStdString());
    }
    if (candidateMethodUids.empty())
        return Result<int, ApplicationError>::success(0);   // 无召回 = 无建议（诚实）

    std::string payload =
        "{\"mel_uid\":\"" + melUid.value() + "\",\"_instruction\":\""
        + jsonEscape(std::string(methodContractHint) + " 候选方法版本 uid："
                     + calibration.value().knowledgeVersionsJson)
        + "\",\"tasks\":[";
    for (size_t i = 0; i < tasks.size(); ++i) {
        if (i)
            payload += ",";
        payload += "{\"task_uid\":\"" + tasks[i].uid.value() + "\",\"title\":\""
                   + jsonEscape(tasks[i].title) + "\"}";
    }
    const std::string fullPayload = payload + "]}";

    const auto job = runJob("method_suggestions", "method_suggestions_v1", fullPayload,
                            calibration.value());
    if (!job)
        return Result<int, ApplicationError>::failure(job.error());

    // 校验并绑定：任务必须属于本 MEL；方法必须来自召回候选
    int bound = 0;
    const QJsonObject result =
        QJsonDocument::fromJson(QByteArray::fromStdString(job.value().userText)).object();
    for (const auto &value : result.value(QStringLiteral("suggestions")).toArray()) {
        const QJsonObject suggestion = value.toObject();
        const std::string taskUid = suggestion.value(QStringLiteral("task_uid")).toString().toStdString();
        const std::string methodUid =
            suggestion.value(QStringLiteral("method_version_uid")).toString().toStdString();
        const auto parsedTask = Domain::Uid::parse(taskUid);
        bool taskBelongs = false;
        for (const auto &task : tasks)
            if (parsedTask && task.uid == *parsedTask)
                taskBelongs = true;
        if (!taskBelongs)
            continue;   // 引用不属于本 MEL 的任务 → 跳过
        if (std::find(candidateMethodUids.begin(), candidateMethodUids.end(), methodUid)
            == candidateMethodUids.end())
            continue;   // 凭空引用未召回方法 → 拒绝（DR-013）

        Domain::MelTaskMethod binding;
        binding.taskUid = *parsedTask;
        binding.methodVersionUid = methodUid;
        binding.rank = bound;
        binding.reason = suggestion.value(QStringLiteral("reason")).toString().toStdString();
        binding.applicabilityJson = std::string("{\"applicability\":\"")
                                    + suggestion.value(QStringLiteral("applicability"))
                                          .toString()
                                          .toStdString()
                                    + "\",\"risks\":\""
                                    + suggestion.value(QStringLiteral("risks"))
                                          .toString()
                                          .toStdString()
                                    + "\"}";
        const auto saved = m_mels.insertTaskMethod(binding);
        if (saved.ok)
            ++bound;
    }

    Audit::record({"ai", {}, "mel.method_suggestions_bound", "mel", melUid.value(),
                   "{\"bound\":" + std::to_string(bound) + "}"});
    return Result<int, ApplicationError>::success(bound);
}

Result<AiPlanningUseCases::ProposalOutput, ApplicationError>
AiPlanningUseCases::askAdvisor(const Domain::Uid &userId, const std::string &question)
{
    Q_UNUSED(userId);
    auto calibration = calibrate("advisor", question, "{\"domain_code\":\"learning\"}",
                                 kKeySubQuestions);
    if (!calibration)
        return Result<ProposalOutput, ApplicationError>::failure(calibration.error());

    const std::string instruction =
        jsonEscape(protocolPrompt(calibration.value().config) + std::string(" ")
                   + std::string(advisorContractHint));
    const std::string payload =
        "{\"_instruction\":\"" + instruction + "\",\"question\":\""
        + jsonEscape(question) + "\",\"_knowledge\":"
        + calibration.value().knowledgeSummary + "}";
    const auto job = runJob("advisor_answer", "generic_v1", payload, calibration.value());
    if (!job)
        return Result<ProposalOutput, ApplicationError>::failure(job.error());

    // 决策记录：答案与知识支持状态可追溯；不自动执行任何操作
    Domain::DecisionRecord decision;
    decision.uid = m_uids.next();
    decision.decisionType = "advisor_answer";
    decision.aggregateType = "advisor";
    decision.aggregateUid = decision.uid.value();
    decision.inputSnapshotJson = "{\"question\":\"" + jsonEscape(question) + "\"}";
    decision.candidateJson = job.value().userText;
    decision.rationale = "知识校准后的咨询回答（支持状态见 source_mode）";
    decision.sourceMode = calibration.value().sourceMode;
    decision.warningJson =
        calibration.value().hasMaterialConflict ? "{\"conflict\":true}" : "{}";
    decision.userStatus = Domain::DecisionUserStatus::NotRequired;
    decision.createdAt = m_clock.utcIso();
    const auto savedDecision = m_decisions.insertDecision(decision);
    if (!savedDecision.ok)
        return Result<ProposalOutput, ApplicationError>::failure(savedDecision.error);

    ProposalOutput output = job.value();
    output.decisionUid = decision.uid.value();
    return Result<ProposalOutput, ApplicationError>::success(std::move(output));
}

Result<void, ApplicationError> AiPlanningUseCases::markDecision(
    const std::string &decisionUid, const std::string &userStatus,
    const std::optional<std::string> &selectedJson)
{
    const auto parsed = Domain::Uid::parse(decisionUid);
    if (!parsed)
        return Result<void, ApplicationError>::failure(
            {ErrorCode::Validation, "invalid decision uid", {}, false});
    const auto current = m_decisions.findDecision(*parsed);
    if (!current)
        return Result<void, ApplicationError>::failure(
            {ErrorCode::NotFound, "decision not found", {}, false});
    const auto saved = m_decisions.updateDecisionStatus(*parsed, userStatus, selectedJson);
    if (!saved.ok)
        return Result<void, ApplicationError>::failure(saved.error);
    Audit::record({"user", {}, "decision.responded", "decision", decisionUid,
                   "{\"status\":\"" + userStatus + "\"}"});
    return Result<void, ApplicationError>::success();
}

} // namespace PersonOS::Application
