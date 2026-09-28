// 模块：领域配置运行时（DR-038；domain-configuration-design-v1.md DC-01..DC-09 的一部分）
// 覆盖：清单加载与 JSON Schema 结构验证、引用/能力检查、参数解析与范围、
//       坏清单拒绝（保留原有效版本）、状态上下文时效三态、候选决策硬约束校验。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>

#include "application/foundation/JsonSchemaValidator.h"
#include "application/usecases/domain/DecisionValidator.h"
#include "application/usecases/domain/DomainRegistry.h"
#include "application/usecases/domain/StateContextBuilder.h"
#include "application/usecases/state/StateUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/knowledge/StateDefinitionsSeed.h"
#include "infrastructure/persistence/SqlDomainManifestRepository.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlStateRepository.h"

using namespace PersonOS;

namespace {
const char *kUserUid = "00000000-0000-0000-0000-0000000000aa";
} // namespace

class TstDomainRuntime : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_domain_runtime.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
        QVERIFY(Infrastructure::LearningManifestSeed(DatabaseManager::instance().database(),
                                                     m_clock)
                    .ensureSeeded());
        QVERIFY(Infrastructure::StateDefinitionsSeed(DatabaseManager::instance().database(),
                                                     m_clock)
                    .ensureSeeded());

        QSqlQuery user(DatabaseManager::instance().database());
        const bool userOk = user.exec(QStringLiteral(
            "INSERT INTO user_profiles_v3(uid,timezone_id,locale,onboarding_status,profile_json,"
            "created_at,updated_at) VALUES('00000000-0000-0000-0000-0000000000aa',"
            "'Asia/Shanghai','zh-CN','complete','{}','2026-09-27T00:00:00Z','2026-09-27T00:00:00Z')"));
        if (!userOk)
            QFAIL(qPrintable(QStringLiteral("user insert: %1").arg(user.lastError().text())));

        QSqlQuery goal(DatabaseManager::instance().database());
        const bool goalOk = goal.exec(QStringLiteral(
            "INSERT INTO goals_v3(uid,user_id,domain_manifest_id,title,description,goal_type,"
            "status,priority,desired_level_json,user_defined_level,sort_order,created_at,"
            "updated_at) SELECT '00000000-0000-0000-0000-0000000000ca',u.id,dm.id,'运行时目标',"
            "'','course','active',1,'{}',1,0,'2026-09-27T00:00:00Z','2026-09-27T00:00:00Z' "
            "FROM user_profiles_v3 u, domain_manifests_v3 dm WHERE "
            "u.uid='00000000-0000-0000-0000-0000000000aa' AND dm.domain_code='learning'"));
        if (!goalOk)
            QFAIL(qPrintable(QStringLiteral("goal insert: %1").arg(goal.lastError().text())));
    }

    void jsonSchemaValidatorSubset()
    {
        const char *schema = R"JSON({"type":"object","required":["name","items"],
            "additionalProperties":false,
            "properties":{"name":{"type":"string","minLength":2},
                          "items":{"type":"array","items":{"type":"object"}},
                          "level":{"type":"integer","minimum":1,"maximum":5},
                          "mode":{"enum":["a","b"]}}})JSON";

        // 合法实例
        QVERIFY(!Application::JsonSchemaValidator::validate(
            schema, R"JSON({"name":"ok","items":[{},{}],"level":3,"mode":"a"})JSON"));

        // 缺 required
        QVERIFY(Application::JsonSchemaValidator::validate(
                    schema, R"JSON({"name":"ok"})JSON")
                    .has_value());
        // 类型不符
        QVERIFY(Application::JsonSchemaValidator::validate(
                    schema, R"JSON({"name":3,"items":[]})JSON")
                    .has_value());
        // additionalProperties 拒绝
        QVERIFY(Application::JsonSchemaValidator::validate(
                    schema, R"JSON({"name":"ok","items":[],"extra":1})JSON")
                    .has_value());
        // 越界
        QVERIFY(Application::JsonSchemaValidator::validate(
                    schema, R"JSON({"name":"ok","items":[],"level":9})JSON")
                    .has_value());
        // enum 外
        QVERIFY(Application::JsonSchemaValidator::validate(
                    schema, R"JSON({"name":"ok","items":[],"mode":"c"})JSON")
                    .has_value());
    }

    void registryLoadsAndValidates()
    {
        Infrastructure::SqlDomainManifestRepository repo(DatabaseManager::instance().database());
        Application::DomainRegistry registry(repo);

        // 学习清单：结构 + 引用 + 能力全部通过
        const auto learning = registry.load("learning");
        if (!learning)
            QFAIL(qPrintable(QString::fromStdString(learning.error().message + ": "
                                                    + learning.error().detail)));
        QVERIFY(!learning.value().workflows.empty());
        QVERIFY(learning.value().capabilities.size() >= 2);
        // 参数解析：mel_period_days 默认 3、范围 1..14（不写死在代码里）
        const auto period = learning.value().parameter("mel_period_days");
        QVERIFY(period.has_value());
        QCOMPARE(period->defaultValue.value_or(-1), 3.0);
        QVERIFY(period->min.has_value() && *period->min == 1.0);
        QVERIFY(period->max.has_value() && *period->max == 14.0);
        QVERIFY(period->userAdjustable);

        // 坏清单（声明未实现能力）→ 加载失败并给出原因；原有效版本仍可加载
        QSqlQuery insert(DatabaseManager::instance().database());
        const bool ok = insert.exec(QStringLiteral(
            "INSERT INTO domain_manifest_versions_v3(uid,manifest_id,version_no,status,"
            "schema_version,manifest_json,content_hash,valid_from,created_at,created_by) "
            "SELECT '00000000-0000-0000-0000-0000000000fa',m.id,99,'active','1.1',"
            "replace(mv.manifest_json,'\"goal_route\",','\"goal_route\","
            "\"not_implemented\",'),'badhash','2026-09-28T00:00:00Z','2026-09-28T00:00:00Z',"
            "'test' FROM domain_manifests_v3 m, domain_manifest_versions_v3 mv "
            "WHERE m.domain_code='learning' AND mv.version_no=1"));
        if (!ok)
            QFAIL(qPrintable(QStringLiteral("bad manifest insert: %1").arg(insert.lastError().text())));

        const auto bad = registry.load("learning");
        QVERIFY(!bad);   // 最新版本 99 无效 → 加载失败（不静默回退伪造结果）

        // 指定版本加载：无效/不存在的版本 → NotFound（不静默回退伪造结果）
        QVERIFY(!registry.loadVersion("00000000-0000-0000-0000-0000000000fb"));
        QSqlQuery restore(DatabaseManager::instance().database());
        QVERIFY(restore.exec(QStringLiteral(
            "DELETE FROM domain_manifest_versions_v3 WHERE version_no=99")));
        QVERIFY(registry.load("learning"));
    }

    void stateContextFreshness()
    {
        Infrastructure::SqlStateRepository stateRepo(DatabaseManager::instance().database(),
                                                     m_clock);
        Application::StateUseCases stateUseCases(stateRepo, m_uids, m_clock);
        Application::StateContextBuilder builder(stateRepo, m_clock);
        const auto userUid = *Domain::Uid::parse(kUserUid);

        // 未记录 → unknown
        const auto before = builder.items(userUid, "2026-09-28T12:00:00Z");
        QVERIFY(before);
        bool energyUnknown = false;
        for (const auto &item : before.value())
            if (item.code == "energy")
                energyUnknown = (item.freshness == "unknown");
        QVERIFY(energyUnknown);

        // 记录 energy → current（TTL 内）
        Application::StateUseCases::ObserveInput input;
        input.userId = userUid;
        input.definitionCode = "energy";
        input.valueJson = std::string("{\"value\":4,\"min\":1,\"max\":5}");
        input.source = Domain::StateSource::Explicit;
        input.idempotencyKey = "ctx:energy:1";
        QVERIFY(stateUseCases.observe(input));

        const std::string nowIso = m_clock.utcIso();
        const auto after = builder.items(userUid, nowIso);
        QVERIFY(after);
        bool energyCurrent = false;
        for (const auto &item : after.value())
            if (item.code == "energy")
                energyCurrent = (item.freshness == "current");
        QVERIFY(energyCurrent);

        // 过期时点（10 天后，超过默认 TTL）→ last_known（不伪装成当前事实）
        const auto stale = builder.items(userUid, m_clock.utcIsoPlusMinutes(1440 * 10));
        QVERIFY(stale);
        bool energyLastKnown = false;
        for (const auto &item : stale.value())
            if (item.code == "energy")
                energyLastKnown = (item.freshness == "last_known");
        QVERIFY(energyLastKnown);

        // 序列化上下文
        const auto json = builder.build(userUid, nowIso);
        QVERIFY(json);
        QVERIFY(QJsonDocument::fromJson(QString::fromStdString(json.value()).toUtf8())
                    .isObject());
    }

    void decisionValidatorHardConstraints()
    {
        Infrastructure::SqlDomainManifestRepository manifestRepo(
            DatabaseManager::instance().database());
        Application::DomainRegistry registry(manifestRepo);
        const auto learning = registry.load("learning");
        QVERIFY(learning);

        Infrastructure::SqlGoalRepository goalsRepo(DatabaseManager::instance().database(),
                                                    m_clock);
        Application::DecisionValidator validator(goalsRepo);
        const auto userUid = *Domain::Uid::parse(kUserUid);

        const std::string goodGoalUid = [&]() {
            QSqlQuery q(DatabaseManager::instance().database());
            q.exec(QStringLiteral("SELECT uid FROM goals_v3 WHERE title='运行时目标'"));
            q.next();
            return q.value(0).toString().toStdString();
        }();

        // 合法 MEL 候选（周期 3 天在领域范围 1..14 内）
        const std::string validProposal =
            std::string("{\"title\":\"t\",\"tasks\":[{\"title\":\"task1\"}],")
            + "\"rationale\":\"r\",\"capacity_min\":300,\"reserve_min\":30,"
            + "\"period_days\":3,\"goal_uid\":\"" + goodGoalUid
            + "\",\"source_mode\":\"ungrounded\"}";
        const auto valid = validator.validate("mel_proposal_v1", validProposal,
                                              learning.value(), userUid);
        QVERIFY2(valid.ok, [&]() {
            std::string all;
            for (const auto &e : valid.errors)
                all += e + ";";
            return all.c_str();
        }());

        // 周期超领域上限 → 错误（配置决定范围，代码不写死）
        std::string badPeriod = validProposal;
        const auto pos = badPeriod.find("\"period_days\":3");
        const std::string badPeriodProposal =
            badPeriod.replace(pos, strlen("\"period_days\":3"), "\"period_days\":99");
        const auto invalidPeriod = validator.validate("mel_proposal_v1", badPeriodProposal,
                                                      learning.value(), userUid);
        QVERIFY(!invalidPeriod.ok);
        QVERIFY(!invalidPeriod.errors.empty());

        // 空任务 → 错误
        const std::string noTasks =
            std::string("{\"title\":\"t\",\"tasks\":[],\"rationale\":\"r\",")
            + "\"capacity_min\":300,\"reserve_min\":30,\"period_days\":3,\"goal_uid\":\""
            + goodGoalUid + "\",\"source_mode\":\"ungrounded\"}";
        QVERIFY(!validator.validate("mel_proposal_v1", noTasks, learning.value(), userUid).ok);

        // 不存在的目标引用 → 错误（引用完整性）
        std::string badGoal = validProposal;
        const auto goalPos = badGoal.find(goodGoalUid);
        const std::string badGoalProposal = badGoal.replace(
            goalPos, goodGoalUid.size(), "00000000-0000-0000-0000-0000000000ff");
        QVERIFY(!validator.validate("mel_proposal_v1", badGoalProposal, learning.value(),
                                    userUid)
                     .ok);

        // 非法 source_mode → 错误
        std::string badMode = validProposal;
        const auto modePos = badMode.find("ungrounded");
        const std::string badModeProposal =
            badMode.replace(modePos, strlen("ungrounded"), "fake_mode");
        QVERIFY(!validator.validate("mel_proposal_v1", badModeProposal, learning.value(),
                                    userUid)
                     .ok);

        // 合法路线候选
        const std::string validRoute =
            std::string("{\"rationale\":\"r\",\"evidence_summary\":\"e\",")
            + "\"stages\":[{\"title\":\"s1\"}],\"goal_uid\":\"" + goodGoalUid
            + "\",\"source_mode\":\"ungrounded\"}";
        QVERIFY(validator.validate("route_proposal_v1", validRoute, learning.value(), userUid)
                    .ok);
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstDomainRuntime)
#include "tst_domain_runtime.moc"
