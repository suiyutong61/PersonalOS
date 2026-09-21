#include "ai/AiConfig.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace PersonOS {

namespace {
QString fileConfigPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/ai_config.json");
}
} // namespace

AiConfig AiConfig::load()
{
    AiConfig config;

    // 1. 环境变量优先（测试/CI 场景）
    const QString envKey = qEnvironmentVariable("PERSONOS_AI_KEY");
    if (!envKey.isEmpty()) {
        config.apiKey = envKey;
        config.baseUrl = qEnvironmentVariable("PERSONOS_AI_BASE_URL", config.baseUrl);
        config.model = qEnvironmentVariable("PERSONOS_AI_MODEL", config.model);
        return config;
    }

    // 2. 本地配置文件（应用数据目录，与数据库同目录）
    QFile file(fileConfigPath());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
        config.apiKey = obj.value(QStringLiteral("apiKey")).toString();
        config.baseUrl = obj.value(QStringLiteral("baseUrl")).toString(config.baseUrl);
        config.model = obj.value(QStringLiteral("model")).toString(config.model);
    }

    // 3. 默认 baseUrl/model（DeepSeek），apiKey 为空表示未配置
    return config;
}

} // namespace PersonOS
