// 模块：OpenAI 兼容 Provider 适配器契约测试（DR-025；AI 契约测试不依赖在线模型）
// 覆盖：真实 HTTP 请求路径/鉴权头/请求体、双通道解析（整段 JSON 与首块 JSON）、
//       凭据不可用、HTTP 错误 → 明确失败信息（不伪造结果）。
#include <QtTest>

#include <QTcpServer>
#include <QTcpSocket>

#include "application/ports/AiPorts.h"
#include "infrastructure/ai/OpenAiCompatibleProvider.h"

using namespace PersonOS;

namespace {

// 内存凭据库替身（凭据只在系统凭据库；测试不落盘）
class FakeCredentials : public Application::CredentialStorePort
{
public:
    std::string secret;
    bool failRead = false;

    Application::Result<void, Application::ApplicationError> write(const std::string &,
                                                                   const std::string &) override
    {
        return Application::Result<void, Application::ApplicationError>::success();
    }
    Application::Result<std::string, Application::ApplicationError> read(
        const std::string &) override
    {
        if (failRead)
            return Application::Result<std::string, Application::ApplicationError>::failure(
                {Application::ErrorCode::ExternalUnavailable, "no credential", {}, false});
        return Application::Result<std::string, Application::ApplicationError>::success(secret);
    }
    Application::Result<void, Application::ApplicationError> remove(const std::string &) override
    {
        return Application::Result<void, Application::ApplicationError>::success();
    }
};

// 简易 HTTP 模拟服务：收齐请求（按 Content-Length）后一次性应答
struct HttpMock
{
    QTcpServer server;
    QString capturedAuth;
    QString capturedPath;
    QByteArray capturedBody;
    QByteArray responseBody;
    int responseStatus = 200;

    bool start()
    {
        if (!server.listen(QHostAddress::LocalHost, 0))
            return false;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this]() {
            QTcpSocket *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                capturedBody += socket->readAll();
                const int headerEnd = capturedBody.indexOf("\r\n\r\n");
                if (headerEnd < 0)
                    return;
                int contentLength = 0;
                const QList<QByteArray> lines = capturedBody.left(headerEnd).split('\r');
                for (const QByteArray &line : lines) {
                    const QByteArray header = line.trimmed();
                    if (header.startsWith("Authorization:"))
                        capturedAuth = QString::fromUtf8(header.mid(15));
                    else if (header.startsWith("POST"))
                        capturedPath = QString::fromUtf8(header).section(' ', 1, 1);
                    else if (header.startsWith("Content-Length:"))
                        contentLength = header.mid(15).trimmed().toInt();
                }
                if (capturedBody.size() < headerEnd + 4 + contentLength)
                    return;   // 请求体未收全
                const QByteArray statusLine = responseStatus == 200
                                                  ? QByteArrayLiteral("HTTP/1.1 200 OK")
                                                  : QByteArrayLiteral("HTTP/1.1 401 Unauthorized");
                socket->write(statusLine + "\r\nContent-Type: application/json\r\n"
                              "Content-Length: "
                              + QByteArray::number(responseBody.size())
                              + "\r\nConnection: close\r\n\r\n" + responseBody);
                socket->disconnectFromHost();
            });
        });
        return true;
    }

    quint16 port() const { return server.serverPort(); }
    void close() { server.close(); }
};

} // namespace

class TstProvider : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); }

    void mockServerRoundTrip()
    {
        HttpMock mock;
        QVERIFY(mock.start());
        mock.responseBody =
            QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\"{\\\"user_text\\\":\\\"hi\\\"}\"}}]}");

        FakeCredentials credentials;
        credentials.secret = "test-key-123";
        Infrastructure::OpenAiCompatibleProvider provider(credentials);

        Domain::AiProviderConfig config;
        config.endpoint = QStringLiteral("http://127.0.0.1:%1/v1").arg(mock.port()).toStdString();
        config.model = "test-model";
        config.credentialRef = "ref-1";
        Application::ProviderRequest request;
        request.systemPrompt = "sys";
        request.userPrompt = "{\"question\":\"你好\"}";
        request.contractType = "generic_v1";
        request.contractVersion = "1";

        const auto response = provider.submit(config, request);
        QVERIFY2(response.ok, response.errorMessage.c_str());
        QCOMPARE(mock.capturedAuth, QStringLiteral("Bearer test-key-123"));
        QVERIFY(mock.capturedPath.endsWith(QStringLiteral("/chat/completions")));
        QVERIFY(mock.capturedBody.contains("test-model"));
        QVERIFY(mock.capturedBody.contains("你好"));
        // 双通道：整段 JSON → 结构化通道
        QCOMPARE(QString::fromStdString(response.structuredJson),
                 QStringLiteral("{\"user_text\":\"hi\"}"));
        QCOMPARE(QString::fromStdString(response.userText),
                 QStringLiteral("{\"user_text\":\"hi\"}"));
        mock.close();
    }

    void firstJsonBlockExtraction()
    {
        HttpMock mock;
        QVERIFY(mock.start());
        mock.responseBody =
            QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\"说明文字 {\\\"user_text\\\":\\\"ok\\\"} 结尾\"}}]}");

        FakeCredentials credentials;
        credentials.secret = "k";
        Infrastructure::OpenAiCompatibleProvider provider(credentials);
        Domain::AiProviderConfig config;
        config.endpoint = QStringLiteral("http://127.0.0.1:%1/v1").arg(mock.port()).toStdString();
        config.model = "m";
        config.credentialRef = "r";
        Application::ProviderRequest request;
        request.systemPrompt = "s";
        request.userPrompt = "q";
        const auto response = provider.submit(config, request);
        QVERIFY2(response.ok, response.errorMessage.c_str());
        // 提取首个 {...} 块
        QCOMPARE(QString::fromStdString(response.structuredJson),
                 QStringLiteral("{\"user_text\":\"ok\"}"));
        mock.close();
    }

    void credentialMissingFailsCleanly()
    {
        FakeCredentials credentials;
        credentials.failRead = true;
        Infrastructure::OpenAiCompatibleProvider provider(credentials);
        Domain::AiProviderConfig config;
        config.endpoint = "http://127.0.0.1:1/v1";
        config.model = "m";
        config.credentialRef = "r";
        Application::ProviderRequest request;
        const auto response = provider.submit(config, request);
        QVERIFY(!response.ok);
        QVERIFY(!response.errorMessage.empty());
    }

    void httpErrorFailsCleanly()
    {
        HttpMock mock;
        QVERIFY(mock.start());
        mock.responseStatus = 401;
        mock.responseBody = QByteArray();

        FakeCredentials credentials;
        credentials.secret = "k";
        Infrastructure::OpenAiCompatibleProvider provider(credentials);
        Domain::AiProviderConfig config;
        config.endpoint = QStringLiteral("http://127.0.0.1:%1/v1").arg(mock.port()).toStdString();
        config.model = "m";
        config.credentialRef = "r";
        Application::ProviderRequest request;
        const auto response = provider.submit(config, request);
        QVERIFY(!response.ok);
        QVERIFY(!response.errorMessage.empty());
        mock.close();
    }
};

QTEST_GUILESS_MAIN(TstProvider)
#include "tst_provider.moc"
