#include "application/usecases/domain/DecisionValidator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "application/foundation/ContractValidator.h"

namespace PersonOS::Application {

namespace {

// 引用完整性：goal_uid / route_uid 必须真实存在（引用检查失败不得写入正式数据）
} // namespace

DecisionValidator::DecisionValidator(GoalRepository &goals) : m_goals(goals) {}

DecisionValidator::ValidationOutcome DecisionValidator::validate(
    const std::string &contractType, const std::string &jsonText,
    const DomainConfig &domain, const Domain::Uid &userId)
{
    Q_UNUSED(userId);
    ValidationOutcome outcome;

    // 1) 契约结构（DR-015：独立带版本号的响应结构）
    if (const auto error = ContractValidator::validate(contractType, jsonText)) {
        outcome.errors.push_back(*error);
        return outcome;
    }

    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(jsonText));
    const QJsonObject object = document.object();

    if (contractType == "mel_proposal_v1") {
        // 2) 硬约束：容量与休息余量、任务非空、周期在领域参数范围内
        const double capacity = object.value(QStringLiteral("capacity_min")).toDouble();
        const double reserve = object.value(QStringLiteral("reserve_min")).toDouble();
        if (capacity < 0)
            outcome.errors.push_back("capacity_min must be >= 0");
        if (reserve < 0 || reserve > capacity)
            outcome.errors.push_back("reserve_min must be within [0, capacity_min]");

        const QJsonArray tasks = object.value(QStringLiteral("tasks")).toArray();
        if (tasks.isEmpty())
            outcome.errors.push_back("mel proposal must contain at least one task");
        for (const auto &value : tasks) {
            const QString title = value.toObject().value(QStringLiteral("title")).toString();
            if (title.trimmed().isEmpty()) {
                outcome.errors.push_back("task title must not be empty");
                break;
            }
        }

        const double periodDays = object.value(QStringLiteral("period_days")).toDouble();
        if (const auto spec = domain.parameter("mel_period_days")) {
            if (spec->min && periodDays < *spec->min)
                outcome.errors.push_back("period_days below domain minimum "
                                         + std::to_string(*spec->min));
            if (spec->max && periodDays > *spec->max)
                outcome.errors.push_back("period_days above domain maximum "
                                         + std::to_string(*spec->max));
        } else {
            // 领域参数缺失：不写死默认周期，标记警告（待配置/AI 依据补充）
            outcome.warnings.push_back("mel_period_days not specified in domain manifest");
        }

        // 3) 引用完整性
        const QString goalUid = object.value(QStringLiteral("goal_uid")).toString();
        if (goalUid.isEmpty()) {
            outcome.errors.push_back("goal_uid is required");
        } else if (const auto parsed = Domain::Uid::parse(goalUid.toStdString())) {
            if (!m_goals.findByUid(*parsed))
                outcome.errors.push_back("goal_uid does not exist");
        } else {
            outcome.errors.push_back("goal_uid is not a valid uid");
        }

        // 4) 知识支持标记一致（DR-027）
        const QString sourceMode = object.value(QStringLiteral("source_mode")).toString();
        if (!sourceMode.isEmpty() && sourceMode != QStringLiteral("grounded")
            && sourceMode != QStringLiteral("partially_grounded")
            && sourceMode != QStringLiteral("ungrounded"))
            outcome.errors.push_back("source_mode must be grounded/partially_grounded/ungrounded");
    } else if (contractType == "route_proposal_v1") {
        const QJsonArray stages = object.value(QStringLiteral("stages")).toArray();
        if (stages.size() < 3 || stages.size() > 5)
            outcome.errors.push_back("route proposal must contain 3 to 5 coarse stages");
        for (const auto &value : stages) {
            const QString title = value.toObject().value(QStringLiteral("title")).toString();
            if (title.trimmed().isEmpty()) {
                outcome.errors.push_back("stage title must not be empty");
                break;
            }
        }
        const QString goalUid = object.value(QStringLiteral("goal_uid")).toString();
        if (goalUid.isEmpty())
            outcome.errors.push_back("goal_uid is required");
        else if (const auto parsed = Domain::Uid::parse(goalUid.toStdString()))
            if (!m_goals.findByUid(*parsed))
                outcome.errors.push_back("goal_uid does not exist");
        const QString sourceMode = object.value(QStringLiteral("source_mode")).toString();
        if (!sourceMode.isEmpty() && sourceMode != QStringLiteral("grounded")
            && sourceMode != QStringLiteral("partially_grounded")
            && sourceMode != QStringLiteral("ungrounded"))
            outcome.errors.push_back("source_mode must be grounded/partially_grounded/ungrounded");
    } else if (contractType == "progress_update_v2") {
        const QJsonArray updates = object.value(QStringLiteral("task_updates")).toArray();
        if (updates.isEmpty() || updates.size() > 100)
            outcome.errors.push_back("task_updates must contain 1 to 100 items");
        for (const auto &value : updates) {
            const QJsonObject update = value.toObject();
            const double progress = update.value(QStringLiteral("progress")).toDouble(-1.0);
            const QString state = update.value(QStringLiteral("state")).toString();
            if (update.value(QStringLiteral("task_uid")).toString().isEmpty()
                || progress < 0.0 || progress > 1.0
                || (state != QStringLiteral("pending") && state != QStringLiteral("active")
                    && state != QStringLiteral("completed"))
                || (progress == 0.0 && state != QStringLiteral("pending"))
                || (progress > 0.0 && progress < 1.0 && state != QStringLiteral("active"))
                || (progress == 1.0 && state != QStringLiteral("completed"))) {
                outcome.errors.push_back("invalid task update");
                break;
            }
        }
        const double total = object.value(QStringLiteral("mel_progress")).toDouble(-1.0);
        if (total < 0.0 || total > 1.0)
            outcome.errors.push_back("mel_progress must be between 0 and 1");
        if (object.value(QStringLiteral("next_action")).toString().trimmed().isEmpty())
            outcome.errors.push_back("next_action is required");
        if (!object.value(QStringLiteral("execution_complete")).isBool())
            outcome.errors.push_back("execution_complete must be boolean");
        if (object.value(QStringLiteral("user_text")).toString().trimmed().isEmpty())
            outcome.errors.push_back("user_text is required");
        const QString reviewSourceMode =
            object.value(QStringLiteral("source_mode")).toString();
        if (!reviewSourceMode.isEmpty() && reviewSourceMode != QStringLiteral("grounded")
            && reviewSourceMode != QStringLiteral("partially_grounded")
            && reviewSourceMode != QStringLiteral("ungrounded"))
            outcome.errors.push_back("source_mode must be grounded/partially_grounded/ungrounded");
    } else if (contractType == "stage_detail_v1") {
        // 硬约束：数量门禁与标题非空（阶段存在性/路线确认态/资料子集在用例层，
        // 需要运行时检索候选集，不引入构造器依赖）
        const auto countRange = [&](const char *field, int minCount, int maxCount) {
            const QJsonArray items = object.value(QLatin1String(field)).toArray();
            if (items.size() < minCount || items.size() > maxCount)
                outcome.errors.push_back(std::string(field) + " must contain "
                                         + std::to_string(minCount) + " to "
                                         + std::to_string(maxCount) + " items");
            return items;
        };
        countRange("outcomes", 1, 4);
        countRange("criteria", 1, 5);
        const QJsonArray tasks = countRange("tasks", 2, 6);
        for (const auto &value : tasks) {
            const QString title = value.toObject().value(QStringLiteral("title")).toString();
            if (title.trimmed().isEmpty()) {
                outcome.errors.push_back("stage task title must not be empty");
                break;
            }
        }
        const QJsonArray projects = countRange("projects", 0, 3);
        for (const auto &value : projects) {
            const QString title = value.toObject().value(QStringLiteral("title")).toString();
            if (title.trimmed().isEmpty()) {
                outcome.errors.push_back("stage project title must not be empty");
                break;
            }
        }
        const QString stageUid = object.value(QStringLiteral("stage_uid")).toString();
        if (stageUid.trimmed().isEmpty())
            outcome.errors.push_back("stage_uid is required");
        const QString stageSourceMode = object.value(QStringLiteral("source_mode")).toString();
        if (!stageSourceMode.isEmpty() && stageSourceMode != QStringLiteral("grounded")
            && stageSourceMode != QStringLiteral("partially_grounded")
            && stageSourceMode != QStringLiteral("ungrounded"))
            outcome.errors.push_back("source_mode must be grounded/partially_grounded/ungrounded");
    } else {
        outcome.errors.push_back("unsupported contract type: " + contractType);
    }

    outcome.ok = outcome.errors.empty();
    return outcome;
}

} // namespace PersonOS::Application
