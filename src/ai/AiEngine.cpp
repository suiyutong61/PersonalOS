#include "ai/AiEngine.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "ai/OpenAiCompatibleAdapter.h"
#include "database/EventRepository.h"
#include "database/StateRepository.h"
#include "database/TaskRepository.h"

namespace PersonOS {

namespace {

// 从 AI 输出中提取 JSON 对象（容忍前后多余文字）
QJsonObject extractJsonObject(const QString &text)
{
    const int start = text.indexOf(QLatin1Char('{'));
    const int end = text.lastIndexOf(QLatin1Char('}'));
    if (start < 0 || end <= start)
        return {};
    return QJsonDocument::fromJson(text.mid(start, end - start + 1).toUtf8()).object();
}

const QString kReviewSystemPrompt = QStringLiteral(
    "你是 Personal OS（个人成长闭环系统）的日复盘助手。"
    "基于用户提供的当日事实进行复盘分析，严格输出 JSON，不要输出任何其他文字。"
    "必须遵守：1) 只基于给出的事实，不编造数据；2) 区分事实与推测；"
    "3) 不替用户做决定，建议要具体可执行。"
    "输出格式：{\"summary\":\"当日总结\",\"problems\":\"发现的问题\","
    "\"causes\":\"原因分析\",\"next_actions\":\"下一步调整建议\"}");

const QString kProposalSystemPrompt = QStringLiteral(
    "你是 Personal OS 的变更建议助手。根据用户描述的问题，输出一个变更提案 JSON，"
    "不要输出任何其他文字。提案必须：1) 有依据（证据）；2) 评估风险；"
    "3) 不得涉及用户的核心价值观与长期目标（那些只能由用户自己决定）。"
    "输出格式：{\"target_type\":\"修改对象类型(如 strategy/plan/habit)\","
    "\"current_value\":\"现状\",\"proposed_value\":\"建议改为\","
    "\"reason\":\"为什么\",\"evidence\":\"依据数据\","
    "\"expected_effect\":\"预期效果\",\"risk\":\"风险\"}");

} // namespace

AiEngine::AiEngine(QObject *parent)
    : QObject(parent)
{
}

void AiEngine::setAdapter(std::unique_ptr<ProviderAdapter> adapter)
{
    m_adapter = std::move(adapter);
}

void AiEngine::setConfig(const AiConfig &config)
{
    m_config = config;
}

bool AiEngine::isConfigured() const
{
    return m_config.hasKey();
}

void AiEngine::ensureAdapter()
{
    if (!m_adapter)
        m_adapter = std::make_unique<OpenAiCompatibleAdapter>(m_config.baseUrl, m_config.apiKey,
                                                              m_config.model);
}

QString AiEngine::buildDailyFacts(const QString &date) const
{
    // 数据最小化（2.7.16）：只取当日任务 / 事件 / 状态摘要，不上传其他任何数据
    QStringList lines;

    const auto tasks = TaskRepository().getByDate(date);
    if (tasks.empty()) {
        lines << QStringLiteral("任务：无");
    } else {
        lines << QStringLiteral("任务：");
        for (const Task &t : tasks) {
            QString line = QStringLiteral("- %1 [状态=%2").arg(t.title, t.status);
            if (t.plannedMinutes)
                line += QStringLiteral(", 计划 %1 分钟").arg(*t.plannedMinutes);
            if (t.actualMinutes)
                line += QStringLiteral(", 实际 %1 分钟").arg(*t.actualMinutes);
            line += QStringLiteral("]");
            lines << line;
        }
    }

    const auto events = EventRepository().getByDate(date);
    if (events.empty()) {
        lines << QStringLiteral("事件：无");
    } else {
        lines << QStringLiteral("事件：");
        for (const Event &e : events) {
            QString line = QStringLiteral("- [%1] %2").arg(e.type, e.title);
            if (!e.description.isEmpty())
                line += QStringLiteral("（%1）").arg(e.description);
            lines << line;
        }
    }

    if (const auto state = StateRepository().getByDate(date)) {
        QStringList parts;
        if (state->sleepHours)
            parts << QStringLiteral("睡眠 %1h").arg(*state->sleepHours);
        if (state->energy)
            parts << QStringLiteral("精力 %1").arg(*state->energy);
        if (state->focus)
            parts << QStringLiteral("专注 %1").arg(*state->focus);
        if (state->mood)
            parts << QStringLiteral("心情 %1").arg(*state->mood);
        if (!parts.isEmpty())
            lines << QStringLiteral("状态：%1").arg(parts.join(QStringLiteral(" / ")));
    }

    return lines.join(u'\n');
}

void AiEngine::analyzeDailyReview(const QString &date, ReviewCallback callback)
{
    if (!m_config.hasKey()) {
        callback(false, {}, QStringLiteral("AI 未配置（缺少 API key）"));
        return;
    }
    ensureAdapter();

    const QString userPrompt =
        QStringLiteral("日期: %1\n\n--- 当日事实 ---\n%2\n\n请输出日复盘分析 JSON。")
            .arg(date, buildDailyFacts(date));

    m_adapter->chat(kReviewSystemPrompt, userPrompt,
                    [date, callback](bool ok, const QString &content, const QString &error) {
                        if (!ok) {
                            callback(false, {}, error);
                            return;
                        }
                        const QJsonObject obj = extractJsonObject(content);
                        Review review;
                        review.reviewType = QStringLiteral("daily");
                        review.periodStart = date;
                        review.summary = obj.value(QStringLiteral("summary")).toString();
                        review.problems = obj.value(QStringLiteral("problems")).toString();
                        review.causes = obj.value(QStringLiteral("causes")).toString();
                        review.nextActions = obj.value(QStringLiteral("next_actions")).toString();
                        if (review.summary.isEmpty()) {
                            callback(false, {}, QStringLiteral("AI 输出无法解析为复盘 JSON"));
                            return;
                        }
                        callback(true, review, {});
                    });
}

void AiEngine::proposeChange(const QString &userRequest, ProposalCallback callback)
{
    if (!m_config.hasKey()) {
        callback(false, {}, QStringLiteral("AI 未配置（缺少 API key）"));
        return;
    }
    ensureAdapter();

    const QString userPrompt =
        QStringLiteral("用户描述的问题：\n%1\n\n请输出变更提案 JSON。").arg(userRequest);

    m_adapter->chat(kProposalSystemPrompt, userPrompt,
                    [callback](bool ok, const QString &content, const QString &error) {
                        if (!ok) {
                            callback(false, {}, error);
                            return;
                        }
                        const QJsonObject obj = extractJsonObject(content);
                        Proposal proposal;
                        proposal.targetType = obj.value(QStringLiteral("target_type")).toString();
                        proposal.currentValue = obj.value(QStringLiteral("current_value")).toString();
                        proposal.proposedValue = obj.value(QStringLiteral("proposed_value")).toString();
                        proposal.reason = obj.value(QStringLiteral("reason")).toString();
                        proposal.evidence = obj.value(QStringLiteral("evidence")).toString();
                        proposal.expectedEffect =
                            obj.value(QStringLiteral("expected_effect")).toString();
                        proposal.risk = obj.value(QStringLiteral("risk")).toString();
                        proposal.status = QStringLiteral("draft"); // 绝不自动生效（2.7.17）
                        if (proposal.proposedValue.isEmpty() || proposal.reason.isEmpty()) {
                            callback(false, {}, QStringLiteral("AI 输出无法解析为提案 JSON"));
                            return;
                        }
                        callback(true, proposal, {});
                    });
}

} // namespace PersonOS
