#include "presentation/viewmodels/VmSupport.h"

#include <QSqlQuery>

#include <cmath>

namespace PersonOS::Presentation {

std::optional<Domain::Uid> activeUserUid(QSqlDatabase database)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT uid FROM user_profiles_v3 ORDER BY id LIMIT 1")))
        return std::nullopt;
    if (!query.next())
        return std::nullopt;
    return Domain::Uid::parse(query.value(0).toString().toStdString());
}

QString friendlyError(const std::string &message, const std::string &detail)
{
    // 已知错误的友好中文映射（不让原始英文/数据库错误文本直接暴露给用户）
    static const QHash<QString, QString> knownMessages = {
        {QStringLiteral("provider config already exists"),
         QStringLiteral("同一服务商与模型组合已存在连接：请直接使用已有连接，"
                        "或改用其他模型标识")},
    };
    const QString messageText = QString::fromStdString(message);
    if (const auto friendly = knownMessages.constFind(messageText);
        friendly != knownMessages.constEnd())
        return friendly.value();
    QString text = messageText;
    if (!detail.empty()) {
        const QString detailText = QString::fromStdString(detail);
        const int newline = detailText.indexOf(u'\n');
        text += QStringLiteral("（%1）").arg(detailText.left(newline >= 0 ? newline : 120));
    }
    return text;
}

QString percentText(double ratio)
{
    if (!std::isfinite(ratio) || ratio < 0.0)
        return QStringLiteral("0%");
    if (ratio > 1.0)
        ratio = 1.0;
    return QString::number(static_cast<int>(std::lround(ratio * 100.0)))
           + QStringLiteral("%");
}

} // namespace PersonOS::Presentation
