#pragma once

#include <QString>

namespace PersonOS {

// AI 配置（README 2.7.14 可替换性设计）
// 加载优先级：环境变量 → %LOCALAPPDATA%/PersonalOS/PersonalOS/ai_config.json → 默认值
//   PERSONOS_AI_KEY / PERSONOS_AI_BASE_URL / PERSONOS_AI_MODEL
// 注意：apiKey 属敏感信息，ai_config.json 已加入 .gitignore，绝不入库。
struct AiConfig
{
    QString apiKey;
    QString baseUrl = QStringLiteral("https://api.deepseek.com");
    QString model = QStringLiteral("deepseek-chat");

    static AiConfig load();

    bool hasKey() const { return !apiKey.trimmed().isEmpty(); }
};

} // namespace PersonOS
