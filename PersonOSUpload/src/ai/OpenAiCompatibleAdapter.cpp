#include "ai/OpenAiCompatibleAdapter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace PersonOS {

OpenAiCompatibleAdapter::OpenAiCompatibleAdapter(const QString &baseUrl, const QString &apiKey,
                                                 const QString &model, QObject *parent)
    : ProviderAdapter(parent)
    , m_baseUrl(baseUrl)
    , m_apiKey(apiKey)
    , m_model(model)
{
}

void OpenAiCompatibleAdapter::chat(const QString &systemPrompt, const QString &userPrompt,
                                   ChatCallback callback)
{
    QNetworkRequest request(QUrl(m_baseUrl + QStringLiteral("/chat/completions")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Authorization",
                         QStringLiteral("Bearer %1").arg(m_apiKey).toUtf8());
    request.setTransferTimeout(120000); // 2 分钟超时

    QJsonArray messages;
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                {QStringLiteral("content"), systemPrompt}});
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"), userPrompt}});

    QJsonObject body;
    body[QStringLiteral("model")] = m_model;
    body[QStringLiteral("messages")] = messages;
    body[QStringLiteral("stream")] = false;
    body[QStringLiteral("temperature")] = 0.3;

    QNetworkReply *reply = m_nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            callback(false, {},
                     QStringLiteral("网络错误(%1): %2").arg(reply->error()).arg(reply->errorString()));
            return;
        }

        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (obj.contains(QStringLiteral("error"))) {
            callback(false, {},
                     obj.value(QStringLiteral("error")).toObject()
                         .value(QStringLiteral("message")).toString());
            return;
        }

        const QString content = obj.value(QStringLiteral("choices"))
                                    .toArray().first().toObject()
                                    .value(QStringLiteral("message")).toObject()
                                    .value(QStringLiteral("content")).toString();
        if (content.isEmpty()) {
            callback(false, {}, QStringLiteral("AI 返回内容为空"));
            return;
        }
        callback(true, content, {});
    });
}

} // namespace PersonOS
