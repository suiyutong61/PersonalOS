#include "application/foundation/ContractValidator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QString>

namespace PersonOS::Application {

namespace {
struct ContractSpec
{
    const char *type;
    std::vector<ContractValidator::FieldSpec> fields;
};

const std::vector<ContractSpec> &contracts()
{
    static const std::vector<ContractSpec> all = {
        {"mel_proposal_v1",
         {{"title", true, false, false},
          {"tasks", true, false, true},
          {"rationale", true, false, false},
          {"capacity_min", true, true, false},
          {"reserve_min", true, true, false},
          {"period_days", true, true, false}}},
        {"review_analysis_v1",
         {{"summary", true, false, false},
          {"problems", true, false, true},
          {"causes", true, false, true},
          {"next_actions", true, false, true}}},
        {"route_proposal_v1",
         {{"rationale", true, false, false},
          {"evidence_summary", true, false, false},
          {"stages", true, false, true},
          {"goal_uid", true, false, false},
          {"source_mode", true, false, false}}},
        {"method_suggestions_v1", {{"suggestions", true, false, true}}},
        {"stage_detail_v1",
         {{"stage_uid", true, false, false},
          {"outcomes", true, false, true},
          {"tasks", true, false, true},
          {"projects", true, false, true},
          {"criteria", true, false, true},
          {"suggested_materials", true, false, true},
          {"rationale", true, false, false},
          {"source_mode", true, false, false}}},
        {"paper_analysis_v1",
         {{"title", true, false, false},
          {"summary", true, false, false},
          {"study_type", true, false, false},
          {"core_claims", true, false, true},
          {"applicability", true, false, true},
          {"limitations", true, false, true},
          {"credibility", true, false, false},
          {"candidate_methods", true, false, true},
          {"candidate_tips", true, false, true},
          {"evidence_fragments", true, false, true},
          {"unknowns", true, false, true}}},
        {"generic_v1", {{"user_text", true, false, false}}},
        {"progress_update_v2",
         {{"observations", true, false, true},
          {"task_updates", true, false, true},
          {"mel_progress", true, true, false},
          {"learned_contents", true, false, true},
          {"next_action", true, false, false},
          {"execution_complete", true, false, false},
          {"user_text", true, false, false},
          {"source_mode", true, false, false}}},
    };
    return all;
}
} // namespace

const std::vector<ContractValidator::FieldSpec> &ContractValidator::specsFor(
    const std::string &contractType)
{
    static const std::vector<FieldSpec> empty;
    for (const auto &contract : contracts())
        if (contractType == contract.type)
            return contract.fields;
    return empty;
}

std::string ContractValidator::promptSchema(const std::string &contractType,
                                            const std::string &version)
{
    if (contractType == "paper_analysis_v1") {
        return std::string(
            "写作总要求:方法与贴士面向普通用户(不是写给 AI 或学术评审),"
            "全部使用通俗中文,避免学术术语;每条 summary 必须清楚说明"
            "「具体怎么做」和「做了能得到什么好处」(预期收益);"
            "候选方法必须给出适用条件与局限。\n")
            + R"JSON({
  "title": "字符串,论文标题",
  "summary": "字符串,用通俗中文总结本块内容要点(1-3 句)",
  "study_type": "字符串,研究类型(如 meta-analysis/randomized-trial/observational/review/theoretical/unknown)",
  "core_claims": ["字符串,核心结论,每条一句,通俗中文"],
  "applicability": ["字符串,适用人群/情境,通俗中文"],
  "limitations": ["字符串,局限与边界,如实列出,通俗中文"],
  "credibility": "字符串,证据可信度评估及依据",
  "candidate_methods": [{
    "title": "方法名称(通俗,如「睡前 1 小时远离屏幕」)",
    "summary": "做法说明+预期收益,通俗中文,让用户直接看懂做这件事能得到什么好处",
    "applicability": ["字符串,适用条件/适合谁"],
    "limitations": ["字符串,不适用情况/风险"],
    "risk_level": "low|medium|high|unknown",
    "evidence_grade": "A|B|C|D|E|unknown",
    "steps": ["可执行步骤,通俗中文,以「你」开头,直接可照做"]
  }],
  "candidate_tips": [{
    "title": "贴士名称(通俗,如「午睡别超过 20 分钟」)",
    "summary": "小妙招做法+预期收益,通俗中文",
    "use_scenario": {"何时用/什么场景"},
    "risk_level": "low|medium|high|unknown"
  }],
  "evidence_fragments": [{"text": "论文原文逐字摘录(必须原样引用,不概括)", "locator": {"页码或位置"}, "relation": "supports|qualifies|contradicts(限制性证据用 qualifies)", "strength": "0到1之间的数字"}],
  "unknowns": ["字符串,未知项或无法判断的项,如实标注"]
})JSON";
    }
    if (contractType == "generic_v1") {
        return R"JSON({"user_text": "字符串,面向用户的说明"})JSON";
    }
    if (contractType == "progress_update_v2") {
        return std::string(
            "写作总要求:根据用户的自然语言汇报判断每个任务的正式进度。"
            "task_updates 必须覆盖提示词中的全部任务且每个 task_uid 只出现一次;"
            "progress 为0到1,state必须与进度一致:0=pending,0到1之间=active,1=completed;"
            "不要让用户自己估算百分比。mel_progress 是全部 required 任务 progress 的平均值;"
            "execution_complete 仅在全部 required 任务完成时为 true;"
            "actual_minutes 是能从汇报中可靠判断出的本次实际投入分钟数,无法判断填0;"
            "next_action 给出下一步唯一且可执行的动作。\n")
            + R"JSON({
  "observations": ["观察要点,每句一条"],
  "task_updates": [{
    "task_uid": "任务列表中的 uid",
    "progress": 0.6,
    "state": "pending|active|completed",
    "actual_minutes": 30,
    "rationale": "根据用户哪一句汇报作出的判断"
  }],
  "mel_progress": 0.6,
  "learned_contents": ["本次实际完成的内容"],
  "next_action": "下一步唯一动作",
  "execution_complete": false,
  "user_text": "面向用户的完整说明",
  "source_mode": "grounded|partially_grounded|ungrounded"
})JSON";
    }
    if (contractType == "stage_detail_v1") {
        return std::string(
            "写作总要求:这是用户在确认路线后,对某一个粗粒度路线阶段的落地安排。"
            "内容面向「如何真正完成这一阶段」,必须现实、可操作、可验证;"
            "数量严格遵守:outcomes 1~4 条、tasks 2~6 条、projects 0~3 条、criteria 1~5 条;"
            "不要把细碎知识点铺成任务海洋,内部任务要少而实;"
            "criteria 必须是可核对的完成标准,不得写「认真学完」「深入理解」等无法验证的表述;"
            "suggested_materials 的 item_uid 只能取自提示词中给出的候选资料列表,不得凭空编造;"
            "stage_uid 必须原样回显请求中的 stage_uid。\n")
            + R"JSON({
  "stage_uid": "字符串,原样回显请求中的 stage_uid",
  "outcomes": [{"description": "完成本阶段后可验证的结果,写清「能做什么」"}],
  "tasks": [{
    "sequence_no": 1,
    "title": "内部任务名",
    "description": "任务内容与做法",
    "estimated_effort_min": 120
  }],
  "projects": [{"title": "项目/练习名", "description": "做法", "verifiable_result": "可观察的产出"}],
  "criteria": [{"description": "完成标准条目,可核对,不写空洞表述"}],
  "suggested_materials": [{"item_uid": "候选资料列表中的条目 uid", "reason": "推荐理由"}],
  "rationale": "这样安排的依据",
  "source_mode": "grounded|partially_grounded|ungrounded"
})JSON";
    }
    // 其余契约:由必填字段生成最小 JSON 形状
    std::string schema = "{";
    const auto &fields = specsFor(contractType);
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0)
            schema += ", ";
        schema += "\"";
        schema += fields[i].name;
        schema += fields[i].isArray ? "\": [\"字符串\"]" : "\": \"字符串\"";
    }
    schema += "}";
    return schema;
}

std::optional<std::string> ContractValidator::validate(const std::string &contractType,
                                                       const std::string &jsonText)
{
    const auto &fields = specsFor(contractType);
    if (fields.empty())
        return std::string("unsupported contract type: ") + contractType;

    const QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray::fromStdString(jsonText));
    if (!doc.isObject())
        return std::string("structured response must be a JSON object");
    const QJsonObject object = doc.object();

    for (const auto &field : fields) {
        if (!object.contains(QLatin1String(field.name))) {
            if (field.required)
                return std::string("missing required field: ") + field.name;
            continue;
        }
        const QJsonValue value = object.value(QLatin1String(field.name));
        if (field.isNumber && !value.isDouble())
            return std::string("field must be numeric: ") + field.name;
        if (field.isArray && !value.isArray())
            return std::string("field must be array: ") + field.name;
    }
    return std::nullopt;
}

} // namespace PersonOS::Application
