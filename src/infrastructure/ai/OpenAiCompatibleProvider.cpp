#include "infrastructure/ai/OpenAiCompatibleProvider.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace PersonOS::Infrastructure {

namespace {

// 从模型输出中提取结构化 JSON：整段是 JSON 用整段；否则取首个 {...} 块
std::string extractStructuredJson(const std::string &content)
{
    QJsonParseError error{};
    const QByteArray bytes = QByteArray::fromStdString(content);
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error == QJsonParseError::NoError)
        return content;

    const int start = static_cast<int>(content.find('{'));
    if (start < 0)
        return {};
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t i = start; i < content.size(); ++i) {
        const char c = content[i];
        if (inString) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                const std::string candidate = content.substr(start, i - start + 1);
                const QJsonDocument sub = QJsonDocument::fromJson(
                    QByteArray::fromStdString(candidate), &error);
                if (error.error == QJsonParseError::NoError)
                    return candidate;
                return {};
            }
        }
    }
    return {};
}

// 解析 chat/completions 响应:取首个 choice 的 message.content;
// content 为空时回退 reasoning_content(思考模式偶发将答案留在推理通道)
bool parseCompletion(const QByteArray &raw, QString &contentOut, QString &errorOut)
{
    const QJsonDocument document = QJsonDocument::fromJson(raw);
    if (!document.isObject()) {
        errorOut = QStringLiteral("provider response is not valid json");
        return false;
    }
    const QJsonArray choices = document.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        errorOut = QStringLiteral("provider response has no choices");
        return false;
    }
    const QJsonObject message =
        choices.first().toObject().value(QStringLiteral("message")).toObject();
    contentOut = message.value(QStringLiteral("content")).toString();
    if (contentOut.trimmed().isEmpty())
        contentOut = message.value(QStringLiteral("reasoning_content")).toString();
    return true;
}

#ifdef Q_OS_WIN
// WinHTTP 同步 POST:与 PowerShell/浏览器共用系统 TLS 栈(Schannel),
// 避免 Qt 网络栈的握手指纹被 CDN 边缘风控(401 "Authentication Fails
// (governor)")拦截;使用系统代理设置(与 QNAM 默认行为一致)。
struct WinHttpResult
{
    bool ok = false;
    int status = 0;
    QByteArray body;
    QString error;
};

WinHttpResult winHttpPost(const QString &url, const QByteArray &authorization,
                          const QByteArray &bodyBytes, int timeoutMs)
{
    WinHttpResult result;
    const QUrl parsed(url);
    const bool secure = parsed.scheme() == QStringLiteral("https");
    // 仅放行 https 与回环地址的 http(本地契约测试用);生产配置必须是 https
    const bool loopback = parsed.host() == QStringLiteral("127.0.0.1")
                          || parsed.host() == QStringLiteral("localhost");
    if (!parsed.isValid() || parsed.host().isEmpty() || (!secure && !loopback)) {
        result.error = QStringLiteral("unsupported url scheme (https required)");
        return result;
    }
    const std::wstring hostStorage = parsed.host().toStdWString();
    const std::wstring pathStorage =
        (parsed.path() + (parsed.hasQuery() ? QStringLiteral("?") + parsed.query()
                                            : QString())).toStdWString();

    HINTERNET session = WinHttpOpen(L"PersonalOS/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        result.error = QStringLiteral("WinHttpOpen failed");
        return result;
    }
    HINTERNET connection = WinHttpConnect(session, hostStorage.c_str(),
                                          static_cast<INTERNET_PORT>(
                                              parsed.port(secure ? 443 : 80)),
                                          0);
    if (!connection) {
        result.error = QStringLiteral("WinHttpConnect failed");
        WinHttpCloseHandle(session);
        return result;
    }
    HINTERNET requestHandle = WinHttpOpenRequest(connection, L"POST", pathStorage.c_str(),
                                                 nullptr, WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                 secure ? WINHTTP_FLAG_SECURE : 0);
    if (!requestHandle) {
        result.error = QStringLiteral("WinHttpOpenRequest failed");
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption(requestHandle, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                     sizeof(protocols));
    WinHttpSetTimeouts(requestHandle, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    const std::wstring headerStorage =
        QStringLiteral("Content-Type: application/json\r\n"
                       "Authorization: %1\r\n"
                       "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                       "PersonalOS/1.0\r\n")
            .arg(QString::fromUtf8(authorization))
            .toStdWString();
    const BOOL sent = WinHttpSendRequest(
        requestHandle, headerStorage.c_str(), static_cast<DWORD>(-1),
        const_cast<char *>(bodyBytes.constData()), static_cast<DWORD>(bodyBytes.size()),
        static_cast<DWORD>(bodyBytes.size()), 0);
    if (!sent) {
        result.error = QStringLiteral("WinHttpSendRequest failed: %1")
                           .arg(GetLastError());
        WinHttpCloseHandle(requestHandle);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }
    if (!WinHttpReceiveResponse(requestHandle, nullptr)) {
        result.error = QStringLiteral("WinHttpReceiveResponse failed: %1")
                           .arg(GetLastError());
        WinHttpCloseHandle(requestHandle);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(requestHandle,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    result.status = static_cast<int>(status);

    DWORD available = 0;
    while (WinHttpQueryDataAvailable(requestHandle, &available) && available > 0) {
        QByteArray chunk(static_cast<qsizetype>(available), Qt::Uninitialized);
        DWORD read = 0;
        if (!WinHttpReadData(requestHandle, chunk.data(), available, &read) || read == 0)
            break;
        chunk.truncate(static_cast<qsizetype>(read));
        result.body.append(chunk);
    }
    result.ok = result.status > 0;
    WinHttpCloseHandle(requestHandle);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}
#endif // Q_OS_WIN

} // namespace

OpenAiCompatibleProvider::OpenAiCompatibleProvider(
    Application::CredentialStorePort &credentials, QObject *parent)
    : QObject(parent), m_credentials(credentials)
{}

Application::ProviderResponse OpenAiCompatibleProvider::submit(
    const Domain::AiProviderConfig &config, const Application::ProviderRequest &request)
{
    Application::ProviderResponse response;

    // 凭据从系统凭据库读取；不落日志、不落数据库
    const auto secret = m_credentials.read(config.credentialRef);
    if (!secret) {
        response.errorMessage = "credential unavailable";
        return response;
    }

    // 请求体：OpenAI 兼容 chat/completions（任务契约 + 双通道提示）
    QJsonArray messages;
    {
        QJsonObject system;
        system.insert(QStringLiteral("role"), QStringLiteral("system"));
        system.insert(QStringLiteral("content"), QString::fromStdString(request.systemPrompt));
        messages.append(system);
        QJsonObject user;
        user.insert(QStringLiteral("role"), QStringLiteral("user"));
        user.insert(QStringLiteral("content"), QString::fromStdString(request.userPrompt));
        messages.append(user);
    }
    QJsonObject body;
    body.insert(QStringLiteral("model"), QString::fromStdString(config.model));
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("temperature"), 0.2);
    // 契约类响应可能很长(证据摘录+候选方法),显式放宽输出上限,
    // 避免默认上限截断 JSON 导致解析失败(OpenAI 兼容通用参数)
    body.insert(QStringLiteral("max_tokens"), 32768);
    // DeepSeek V4 默认开启思考模式,推理可能耗尽输出预算导致 content 为空、
    // 结构化契约提取失败(真实批次 14/14 复现)。契约类调用显式关闭思考;
    // 该字段为 DeepSeek 专属,其他 OpenAI 兼容服务端点不发送。
    if (QString::fromStdString(config.endpoint).contains(QStringLiteral("deepseek.com"),
                                                         Qt::CaseInsensitive)) {
        QJsonObject thinking;
        thinking.insert(QStringLiteral("type"), QStringLiteral("disabled"));
        body.insert(QStringLiteral("thinking"), thinking);
    }

    const QByteArray bodyBytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
    const QString url = QString::fromStdString(config.endpoint)
                        + QStringLiteral("/chat/completions");
    const QByteArray authorization =
        QByteArrayLiteral("Bearer ") + QByteArray::fromStdString(secret.value());

#ifdef Q_OS_WIN
    // https 用 WinHTTP(系统 TLS 栈),避免 Qt 网络栈指纹被 CDN 风控拦截；
    // http(仅本地契约测试使用)继续走 QNAM 异步,避免阻塞同线程 mock。
    if (url.startsWith(QStringLiteral("https://"))) {
        const WinHttpResult result = winHttpPost(url, authorization, bodyBytes, 60000);
        if (!result.ok) {
            response.errorMessage = result.error.toStdString();
            return response;
        }
        if (result.status < 200 || result.status >= 300) {
            QString message = QStringLiteral("HTTP %1").arg(result.status);
            const QString snippet = QString::fromUtf8(result.body).simplified().left(240);
            if (!snippet.isEmpty())
                message += QStringLiteral(" | body: ") + snippet;
            response.errorMessage = message.toStdString();
            return response;
        }
        QString content;
        QString parseError;
        if (!parseCompletion(result.body, content, parseError)) {
            response.errorMessage = parseError.toStdString();
            return response;
        }
        response.ok = true;
        response.userText = content.toStdString();
        response.structuredJson = extractStructuredJson(content.toStdString());
        return response;
    }
#endif
    QNetworkRequest networkRequest;
    networkRequest.setUrl(QUrl(url));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             QStringLiteral("application/json"));
    networkRequest.setRawHeader(QByteArrayLiteral("Authorization"), authorization);
    networkRequest.setRawHeader(
        QByteArrayLiteral("User-Agent"),
        QByteArrayLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) PersonalOS/1.0"));
    networkRequest.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    networkRequest.setTransferTimeout(60000);

    QNetworkAccessManager manager;
    QEventLoop loop;
    QNetworkReply *reply = manager.post(networkRequest, bodyBytes);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(65000, &loop, &QEventLoop::quit);   // 兜底超时
    loop.exec();
    if (!reply->isFinished())
        reply->abort();

    if (reply->error() != QNetworkReply::NoError) {
        // 携带服务器返回正文摘要(截断并压平空白),便于诊断 CDN 拦截等非标准失败
        const QByteArray errorBody = reply->readAll();
        QString message = reply->errorString();
        if (!errorBody.isEmpty()) {
            const QString snippet = QString::fromUtf8(errorBody).simplified().left(240);
            if (!snippet.isEmpty())
                message += QStringLiteral(" | body: ") + snippet;
        }
        response.errorMessage = message.toStdString();
        reply->deleteLater();
        return response;
    }

    const QByteArray raw = reply->readAll();
    reply->deleteLater();

    QString content;
    QString parseError;
    if (!parseCompletion(raw, content, parseError)) {
        response.errorMessage = parseError.toStdString();
        return response;
    }

    // 双通道：面向用户的说明 + 结构化 JSON（DR-015）
    response.ok = true;
    response.userText = content.toStdString();
    response.structuredJson = extractStructuredJson(content.toStdString());
    return response;
}


Application::Result<std::vector<float>, Application::ApplicationError>
OpenAiCompatibleProvider::embed(const Domain::AiProviderConfig &config,
                                const std::string &text)
{
    const auto secret = m_credentials.read(config.credentialRef);
    if (!secret)
        return Application::Result<std::vector<float>, Application::ApplicationError>::failure(
            {Application::ErrorCode::ExternalUnavailable, "credential unavailable", {}, true});

    QJsonObject body;
    body.insert(QStringLiteral("model"), QString::fromStdString(config.model));
    body.insert(QStringLiteral("input"), QString::fromStdString(text));

    const QByteArray bodyBytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
    const QString url = QString::fromStdString(config.endpoint)
                        + QStringLiteral("/embeddings");
    const QByteArray authorization =
        QByteArrayLiteral("Bearer ") + QByteArray::fromStdString(secret.value());

#ifdef Q_OS_WIN
    if (url.startsWith(QStringLiteral("https://"))) {
        const WinHttpResult result = winHttpPost(url, authorization, bodyBytes, 60000);
        if (!result.ok)
            return Application::Result<std::vector<float>, Application::ApplicationError>::failure(
                {Application::ErrorCode::ExternalUnavailable, result.error.toStdString(), {},
                 true});
        if (result.status < 200 || result.status >= 300) {
            QString message = QStringLiteral("HTTP %1").arg(result.status);
            const QString snippet = QString::fromUtf8(result.body).simplified().left(240);
            if (!snippet.isEmpty())
                message += QStringLiteral(" | body: ") + snippet;
            return Application::Result<std::vector<float>, Application::ApplicationError>::failure(
                {Application::ErrorCode::ExternalUnavailable, message.toStdString(), {}, true});
        }
        const QJsonDocument document = QJsonDocument::fromJson(result.body);
        const QJsonArray data = document.object().value(QStringLiteral("data")).toArray();
        if (data.isEmpty())
            return Application::Result<std::vector<float>, Application::ApplicationError>::failure(
                {Application::ErrorCode::Validation, "embeddings response empty", {}, false});
        const QJsonArray embedding = data.first().toObject().value(QStringLiteral("embedding")).toArray();
        std::vector<float> vector;
        vector.reserve(embedding.size());
        for (const auto &value : embedding)
            vector.push_back(static_cast<float>(value.toDouble()));
        if (vector.empty())
            return Application::Result<std::vector<float>, Application::ApplicationError>::failure(
                {Application::ErrorCode::Validation, "embedding vector empty", {}, false});
        return Application::Result<std::vector<float>, Application::ApplicationError>::success(vector);
    }
#endif
    QNetworkRequest networkRequest;
    networkRequest.setUrl(QUrl(url));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             QStringLiteral("application/json"));
    networkRequest.setRawHeader(QByteArrayLiteral("Authorization"), authorization);
    networkRequest.setRawHeader(
        QByteArrayLiteral("User-Agent"),
        QByteArrayLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) PersonalOS/1.0"));
    networkRequest.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    networkRequest.setTransferTimeout(60000);

    QNetworkAccessManager manager;
    QEventLoop loop;
    QNetworkReply *reply = manager.post(networkRequest, bodyBytes);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(65000, &loop, &QEventLoop::quit);
    loop.exec();
    if (!reply->isFinished())
        reply->abort();
    if (reply->error() != QNetworkReply::NoError) {
        const std::string error = reply->errorString().toStdString();
        reply->deleteLater();
        return Application::Result<std::vector<float>, Application::ApplicationError>::failure(
            {Application::ErrorCode::ExternalUnavailable, error, {}, true});
    }
    const QByteArray raw = reply->readAll();
    reply->deleteLater();

    const QJsonDocument document = QJsonDocument::fromJson(raw);
    const QJsonArray data = document.object().value(QStringLiteral("data")).toArray();
    if (data.isEmpty())
        return Application::Result<std::vector<float>, Application::ApplicationError>::failure(
            {Application::ErrorCode::Validation, "embeddings response empty", {}, false});
    const QJsonArray embedding = data.first().toObject().value(QStringLiteral("embedding")).toArray();
    std::vector<float> vector;
    vector.reserve(embedding.size());
    for (const auto &value : embedding)
        vector.push_back(static_cast<float>(value.toDouble()));
    if (vector.empty())
        return Application::Result<std::vector<float>, Application::ApplicationError>::failure(
            {Application::ErrorCode::Validation, "embedding vector empty", {}, false});
    return Application::Result<std::vector<float>, Application::ApplicationError>::success(vector);
}

} // namespace PersonOS::Infrastructure
