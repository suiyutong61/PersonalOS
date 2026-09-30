// 模块：应用首次启动引导（requirements §8 用户档案；DR-038 领域清单种子；
//       状态定义与复盘问卷种子）
// 覆盖：空库创建默认本地档案、档案/种子已存在时不重复写入、三种子幂等。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/knowledge/ReviewQuestionnaireSeed.h"
#include "infrastructure/knowledge/StateDefinitionsSeed.h"
#include "infrastructure/persistence/UserProfileBootstrap.h"

using namespace PersonOS;

class TstStartupBootstrap : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_startup_bootstrap.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
    }

    void createsProfileWhenMissing()
    {
        QSqlQuery count(DatabaseManager::instance().database());
        QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM user_profiles_v3")));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), 0);

        QVERIFY(Infrastructure::ensureDefaultUserProfile(
            DatabaseManager::instance().database()));

        QSqlQuery verify(DatabaseManager::instance().database());
        QVERIFY(verify.exec(QStringLiteral(
            "SELECT uid, timezone_id, locale, onboarding_status FROM user_profiles_v3")));
        QVERIFY(verify.next());
        QVERIFY(!verify.value(0).toString().isEmpty());
        QVERIFY(!verify.value(1).toString().isEmpty());   // 时区取系统
        QVERIFY(verify.value(2).toString().contains(QLatin1Char('-')));   // zh-CN 形态
        QCOMPARE(verify.value(3).toString(), QStringLiteral("complete"));
        QVERIFY(!verify.next());   // 恰好一行
    }

    void seedsAreIdempotent()
    {
        // 三种子各跑两遍：数量不变、无重复
        Infrastructure::QtSystemClock clock;
        const auto database = DatabaseManager::instance().database();
        const auto manifest =
            Infrastructure::LearningManifestSeed(database, clock).ensureSeeded();
        const auto states =
            Infrastructure::StateDefinitionsSeed(database, clock).ensureSeeded();
        const auto questionnaire =
            Infrastructure::ReviewQuestionnaireSeed(database, clock).ensureSeeded();
        QVERIFY(manifest.hasValue());
        QVERIFY(states.hasValue());
        QVERIFY(questionnaire.hasValue());

        const int manifestsBefore = countOf(QStringLiteral("domain_manifests_v3"));
        const int statesBefore = countOf(QStringLiteral("state_definitions_v4"));
        const int questionnairesBefore = countOf(QStringLiteral("questionnaires_v4"));
        QVERIFY(manifestsBefore > 0);
        QVERIFY(statesBefore > 0);
        QVERIFY(questionnairesBefore > 0);

        QVERIFY(Infrastructure::LearningManifestSeed(database, clock).ensureSeeded());
        QVERIFY(Infrastructure::StateDefinitionsSeed(database, clock).ensureSeeded());
        QVERIFY(Infrastructure::ReviewQuestionnaireSeed(database, clock).ensureSeeded());
        QCOMPARE(countOf(QStringLiteral("domain_manifests_v3")), manifestsBefore);
        QCOMPARE(countOf(QStringLiteral("state_definitions_v4")), statesBefore);
        QCOMPARE(countOf(QStringLiteral("questionnaires_v4")), questionnairesBefore);
    }

    void noOpWhenProfileExists()
    {
        QVERIFY(Infrastructure::ensureDefaultUserProfile(
            DatabaseManager::instance().database()));
        QSqlQuery count(DatabaseManager::instance().database());
        QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM user_profiles_v3")));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), 1);
    }

private:
    int countOf(const QString &table)
    {
        QSqlQuery count(DatabaseManager::instance().database());
        count.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(table));
        count.next();
        return count.value(0).toInt();
    }

    QString m_path;
};

QTEST_GUILESS_MAIN(TstStartupBootstrap)
#include "tst_startup_bootstrap.moc"
