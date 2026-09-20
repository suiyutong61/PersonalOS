#pragma once

#include <QObject>
#include <QString>

#include <functional>

namespace PersonOS {

// AI Provider 适配器接口（README 2.7.14）
// 更换供应商（DeepSeek/OpenAI/本地模型…）不影响 AiEngine 与上层业务。
class ProviderAdapter : public QObject
{
    Q_OBJECT
public:
    using ChatCallback =
        std::function<void(bool ok, const QString &content, const QString &error)>;

    explicit ProviderAdapter(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~ProviderAdapter() override = default;

    // 异步发起一次对话；callback 在事件循环中回调（ok/content/error 三选一有效）
    virtual void chat(const QString &systemPrompt, const QString &userPrompt,
                      ChatCallback callback) = 0;

    virtual QString providerName() const = 0;
};

} // namespace PersonOS
