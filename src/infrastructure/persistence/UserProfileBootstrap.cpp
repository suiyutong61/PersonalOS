#include "infrastructure/persistence/UserProfileBootstrap.h"

#include <QDateTime>
#include <QLocale>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <QUuid>

#include <QtGlobal>

#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/knowledge/ReviewQuestionnaireSeed.h"
#include "infrastructure/knowledge/StateDefinitionsSeed.h"

namespace PersonOS::Infrastructure {

bool ensureDefaultUserProfile(QSqlDatabase database)
{
    QSqlQuery count(database);
    if (!count.exec(QStringLiteral("SELECT COUNT(*) FROM user_profiles_v3")) || !count.next())
        return false;
    if (count.value(0).toInt() > 0)
        return true;

    QSqlQuery insert(database);
    insert.prepare(QStringLiteral(
        "INSERT INTO user_profiles_v3(uid,display_name,timezone_id,locale,onboarding_status,"
        "profile_json,created_at,updated_at) VALUES(?,NULL,?,?,'complete','{}',?,?)"));
    insert.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    // systemTimeZoneId() 返回 QByteArray，直接绑定会被 SQLite 驱动当作 BLOB
    //（STRICT 表 TEXT 列拒绝），必须显式转 QString
    insert.addBindValue(QString::fromUtf8(QTimeZone::systemTimeZoneId()));
    insert.addBindValue(QLocale::system().name().replace(QLatin1Char('_'), QLatin1Char('-')));
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    insert.addBindValue(now);
    insert.addBindValue(now);
    if (!insert.exec()) {
        qWarning("user profile insert failed: %s",
                 insert.lastError().text().toUtf8().constData());
        return false;
    }
    return true;
}

bool ensureStartupSeeded(QSqlDatabase database)
{
    bool ok = ensureDefaultUserProfile(database);
    QtSystemClock clock;
    if (!LearningManifestSeed(database, clock).ensureSeeded()) {
        qWarning("学习领域清单种子失败(启动引导)");
        ok = false;
    }
    if (!StateDefinitionsSeed(database, clock).ensureSeeded()) {
        qWarning("状态定义种子失败(启动引导)");
        ok = false;
    }
    if (!ReviewQuestionnaireSeed(database, clock).ensureSeeded()) {
        qWarning("复盘问卷种子失败(启动引导)");
        ok = false;
    }
    return ok;
}

} // namespace PersonOS::Infrastructure
