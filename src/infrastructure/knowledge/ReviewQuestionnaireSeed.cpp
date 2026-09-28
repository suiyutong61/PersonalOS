#include "infrastructure/knowledge/ReviewQuestionnaireSeed.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

namespace {

const char *kCode = "review_extended_states";
constexpr int kVersionNo = 1;

// 与 StateDefinitionsSeed 的 16 项定义对齐（基本 + 十类拓展，每类至少一项）
const char *kItemCodes[] = {
    "energy",            "focus",            "mood",
    "fatigue",           "sleep_hours",      "available_time_min",
    "cognitive_load",    "stress",           "motivation",
    "recreation_need",   "body_condition",   "environment_quality",
    "social_load",       "behavior_adherence", "system_feedback",
    "domain_progress",
};

QString schemaJson()
{
    QJsonArray items;
    for (const char *code : kItemCodes) {
        QJsonObject item;
        item.insert(QStringLiteral("code"), QString::fromLatin1(code));
        items.append(item);
    }
    QJsonObject schema;
    schema.insert(QStringLiteral("items"), items);
    schema.insert(QStringLiteral("allow_skip"), true);
    schema.insert(QStringLiteral("no_change_is_confirmation"), true);
    return QString::fromUtf8(QJsonDocument(schema).toJson(QJsonDocument::Compact));
}

} // namespace

ReviewQuestionnaireSeed::ReviewQuestionnaireSeed(QSqlDatabase database,
                                                 const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::Result<void, Application::ApplicationError>
ReviewQuestionnaireSeed::ensureSeeded()
{
    QSqlQuery exists(m_database);
    exists.prepare(QStringLiteral(
        "SELECT id FROM questionnaires_v4 WHERE code=? AND version_no=?"));
    exists.addBindValue(QString::fromLatin1(kCode));
    exists.addBindValue(kVersionNo);
    if (!exists.exec())
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "questionnaire query failed",
             exists.lastError().text().toStdString(), false});
    if (exists.next())
        return Application::Result<void, Application::ApplicationError>::success(); // 幂等

    const std::string now = formatUtcIso(m_clock.now());
    const std::string uid =
        QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "INSERT INTO questionnaires_v4(uid, code, version_no, name, schema_json, status, "
        "created_at, updated_at, revision) VALUES(?,?,?,?,?,?,?,?,1)"));
    insert.addBindValue(QString::fromStdString(uid));
    insert.addBindValue(QString::fromLatin1(kCode));
    insert.addBindValue(kVersionNo);
    insert.addBindValue(QStringLiteral("复盘状态核对问卷（基本+十类拓展）"));
    insert.addBindValue(schemaJson());
    insert.addBindValue(QStringLiteral("active"));
    insert.addBindValue(QString::fromStdString(now));
    insert.addBindValue(QString::fromStdString(now));
    if (!insert.exec())
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "questionnaire seed insert failed",
             insert.lastError().text().toStdString(), false});
    return Application::Result<void, Application::ApplicationError>::success();
}

} // namespace PersonOS::Infrastructure
