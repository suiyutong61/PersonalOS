#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

#include "ai/AiConfig.h"
#include "ai/ProviderAdapter.h"
#include "models/Proposal.h"
#include "models/Review.h"

namespace PersonOS {

// AI Engine（README 2.4.9 / 2.7.15）
// Personal OS 的智能能力层，不是最高决策层：
// 只做 信息整理 / 分析 / 生成 Proposal，不直接修改任何正式数据（2.7.17）。
// 数据边界（2.7.16）：只发送任务所需的最小必要数据，绝不上传整个数据库。
class AiEngine : public QObject
{
    Q_OBJECT
public:
    using ReviewCallback =
        std::function<void(bool ok, const Review &review, const QString &error)>;
    using ProposalCallback =
        std::function<void(bool ok, const Proposal &proposal, const QString &error)>;

    explicit AiEngine(QObject *parent = nullptr);

    void setAdapter(std::unique_ptr<ProviderAdapter> adapter);
    void setConfig(const AiConfig &config);

    // 是否已配置 API key（未配置时调用会立即返回错误）
    bool isConfigured() const;

    // 日复盘分析：只发送当日最小必要事实，AI 返回
    // summary/problems/causes/next_actions（JSON → Review，不落库）
    void analyzeDailyReview(const QString &date, ReviewCallback callback);

    // 变更建议：根据用户描述，AI 输出 Proposal 草案（status=draft，绝不自动生效）
    void proposeChange(const QString &userRequest, ProposalCallback callback);

private:
    QString buildDailyFacts(const QString &date) const; // 数据最小化（2.7.16）
    void ensureAdapter();

    std::unique_ptr<ProviderAdapter> m_adapter;
    AiConfig m_config;
};

} // namespace PersonOS
