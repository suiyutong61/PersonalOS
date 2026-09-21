#pragma once

#include <QNetworkAccessManager>
#include <QString>

#include "ai/ProviderAdapter.h"

namespace PersonOS {

// OpenAI 兼容协议适配器（chat/completions）。
// DeepSeek、OpenAI、智谱、通义等供应商均可通过 baseUrl/model 切换。
class OpenAiCompatibleAdapter : public ProviderAdapter
{
    Q_OBJECT
public:
    OpenAiCompatibleAdapter(const QString &baseUrl, const QString &apiKey, const QString &model,
                            QObject *parent = nullptr);

    void chat(const QString &systemPrompt, const QString &userPrompt,
              ChatCallback callback) override;
    QString providerName() const override { return QStringLiteral("openai-compatible"); }

private:
    QNetworkAccessManager m_nam;
    QString m_baseUrl;
    QString m_apiKey;
    QString m_model;
};

} // namespace PersonOS
