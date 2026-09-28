// IMP-003 补充项：长期内容地图与双层进度（requirements R3.2.1；架构 4.3.5、DR-020）
// 覆盖：目录创建/节点/确认、未确认不可作为进度依据、覆盖率口径（等权/加权/
//       跳过不计入）、进度事件追加式与幂等、目标级聚合、状态一致性。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "application/usecases/goal/ContentMapUseCases.h"
#include "application/usecases/goal/GoalUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/persistence/SqlContentMapRepository.h"
#include "infrastructure/persistence/SqlGoalRepository.h"

using namespace PersonOS;

namespace {
const char *kUserUid = "00000000-0000-0000-0000-0000000000aa";
} // namespace

class TstContentMap : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_content_map.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
        QVERIFY(Infrastructure::LearningManifestSeed(DatabaseManager::instance().database(),
                                                     m_clock)
                    .ensureSeeded());

        QSqlQuery user(DatabaseManager::instance().database());
        const bool userOk = user.exec(QStringLiteral(
            "INSERT INTO user_profiles_v3(uid,timezone_id,locale,onboarding_status,profile_json,"
            "created_at,updated_at) VALUES('00000000-0000-0000-0000-0000000000aa',"
            "'Asia/Shanghai','zh-CN','complete','{}','2026-09-27T00:00:00Z','2026-09-27T00:00:00Z')"));
        if (!userOk)
            QFAIL(qPrintable(QStringLiteral("user insert: %1").arg(user.lastError().text())));

        QSqlQuery manifest(DatabaseManager::instance().database());
        QVERIFY(manifest.exec(QStringLiteral(
            "SELECT uid FROM domain_manifests_v3 WHERE domain_code='learning'")));
        QVERIFY(manifest.next());
        const QString manifestUid = manifest.value(0).toString();
        Infrastructure::SqlGoalRepository goalsRepo(DatabaseManager::instance().database(),
                                                    m_clock);
        Application::GoalUseCases goalUseCases(goalsRepo, m_uids, m_clock);
        Application::GoalUseCases::CreateInput goalInput;
        goalInput.userId = *Domain::Uid::parse(kUserUid);
        goalInput.domainManifestId = *Domain::Uid::parse(manifestUid.toStdString());
        goalInput.title = QStringLiteral("内容地图目标").toStdString();
        goalInput.desiredLevelJson = std::string("{}");
        goalInput.userDefinedLevel = true;
        QVERIFY(goalUseCases.createGoal(goalInput));
    }

    Domain::Uid goalUid()
    {
        QSqlQuery q(DatabaseManager::instance().database());
        q.exec(QStringLiteral("SELECT uid FROM goals_v3 WHERE title='内容地图目标'"));
        q.next();
        return *Domain::Uid::parse(q.value(0).toString().toStdString());
    }

    Domain::Uid createMap(const std::string &title)
    {
        Infrastructure::SqlContentMapRepository repo(DatabaseManager::instance().database(),
                                                     m_clock);
        Infrastructure::SqlGoalRepository goalsRepo(DatabaseManager::instance().database(),
                                                    m_clock);
        Application::ContentMapUseCases useCases(repo, goalsRepo, m_uids, m_clock);

        Application::ContentMapUseCases::CreateMapInput input;
        input.userId = *Domain::Uid::parse(kUserUid);
        input.goalId = goalUid();
        input.title = title;
        input.sourceType = "user_document";
        const auto created = useCases.createMap(input);
        if (!created)
            return {};
        return created.value().uid;
    }

    void mapLifecycleAndConfirmation()
    {
        Infrastructure::SqlContentMapRepository repo(DatabaseManager::instance().database(),
                                                     m_clock);
        Infrastructure::SqlGoalRepository goalsRepo(DatabaseManager::instance().database(),
                                                    m_clock);
        Application::ContentMapUseCases useCases(repo, goalsRepo, m_uids, m_clock);
        const auto mapUid = createMap(QStringLiteral("十章节教材").toStdString());
        QVERIFY(!mapUid.empty());

        // 空地图不得确认
        QVERIFY(!useCases.confirmMap(mapUid, 1));

        // 添加十章（等权）
        Application::ContentMapUseCases::AddNodesInput addInput;
        for (int i = 0; i < 10; ++i) {
            Domain::ContentNode node;
            node.title = QStringLiteral("第%1章").arg(i + 1).toStdString();
            node.nodeType = "chapter";
            node.sequenceNo = i;
            node.weight = 1.0;
            addInput.nodes.push_back(std::move(node));
        }
        const auto nodes = useCases.addNodes(mapUid, addInput);
        if (!nodes)
            QFAIL(qPrintable(QString::fromStdString(nodes.error().message + ": "
                                                    + nodes.error().detail)));
        QCOMPARE(nodes.value().size(), 10);

        // 草稿地图可继续加节点；重复 (parent,sequence) 冲突
        Domain::ContentNode dup;
        dup.title = QStringLiteral("第11章").toStdString();
        dup.nodeType = "chapter";
        dup.sequenceNo = 0;
        dup.weight = 1.0;
        Application::ContentMapUseCases::AddNodesInput dupInput;
        dupInput.nodes.push_back(dup);
        QVERIFY(!useCases.addNodes(mapUid, dupInput));

        // 确认 → confirmed；重复确认 → Validation
        const auto confirmed = useCases.confirmMap(mapUid, 1);
        QVERIFY(confirmed);
        QVERIFY(confirmed.value().status == Domain::ContentMapStatus::Confirmed);
        QVERIFY(!useCases.confirmMap(mapUid, 2));

        // 已确认地图不接受新节点
        QVERIFY(!useCases.addNodes(mapUid, dupInput));

        // 未确认地图不得作为进度依据：新草稿地图上的节点更新被拒
        const auto draftMapUid = createMap(QStringLiteral("草稿目录").toStdString());
        QVERIFY(!draftMapUid.empty());
        Domain::ContentNode draftNode;
        draftNode.title = QStringLiteral("草稿章").toStdString();
        draftNode.nodeType = "chapter";
        draftNode.sequenceNo = 0;
        draftNode.weight = 1.0;
        Application::ContentMapUseCases::AddNodesInput draftInput;
        draftInput.nodes.push_back(draftNode);
        const auto draftNodes = useCases.addNodes(draftMapUid, draftInput);
        QVERIFY(draftNodes);
        Application::ContentMapUseCases::UpdateProgressInput progressInput;
        progressInput.userId = *Domain::Uid::parse(kUserUid);
        progressInput.state = Domain::ContentNodeState::Completed;
        progressInput.progress = 1.0;
        progressInput.idempotencyKey = "cm:progress:draft";
        QVERIFY(!useCases.updateProgress(draftNodes.value().front().uid, progressInput));
    }

    void coverageCaliberAndProgress()
    {
        Infrastructure::SqlContentMapRepository repo(DatabaseManager::instance().database(),
                                                     m_clock);
        Infrastructure::SqlGoalRepository goalsRepo(DatabaseManager::instance().database(),
                                                    m_clock);
        Application::ContentMapUseCases useCases(repo, goalsRepo, m_uids, m_clock);
        const auto userUid = *Domain::Uid::parse(kUserUid);

        // 复用"十章节教材"地图（已确认、十章等权）
        QSqlQuery findMap(DatabaseManager::instance().database());
        QVERIFY(findMap.exec(QStringLiteral(
            "SELECT uid FROM content_maps_v3 WHERE title='十章节教材'")));
        QVERIFY(findMap.next());
        const auto mapUid = *Domain::Uid::parse(findMap.value(0).toString().toStdString());

        const auto nodes = useCases.nodesOf(mapUid);
        QVERIFY(nodes);
        QCOMPARE(nodes.value().size(), 10);

        // 初始覆盖率 0；口径说明等权
        const auto initial = useCases.coverage(mapUid, userUid);
        QVERIFY(initial);
        QCOMPARE(initial.value().coverage, 0.0);
        QVERIFY(initial.value().caliberText.find("10") != std::string::npos);
        QVERIFY(initial.value().caliberText.find("等权") != std::string::npos);

        // 完成第一章 → 10%
        const auto completed = update(useCases, nodes.value()[0].uid, userUid,
                                      Domain::ContentNodeState::Completed, 1.0, "cm:progress:c1");
        QVERIFY(completed);
        const auto afterOne = useCases.coverage(mapUid, userUid);
        QVERIFY(afterOne);
        QCOMPARE(afterOne.value().coverage, 0.1);

        // 第二章进行中 50% → 15%
        const auto inProgress = update(useCases, nodes.value()[1].uid, userUid,
                                       Domain::ContentNodeState::InProgress, 0.5,
                                       "cm:progress:c2");
        QVERIFY(inProgress);
        const auto afterTwo = useCases.coverage(mapUid, userUid);
        QVERIFY(afterTwo);
        QCOMPARE(afterTwo.value().coverage, 0.15);

        // 第三章跳过 → 不计入口径分母：1.5/9
        const auto skipped = update(useCases, nodes.value()[2].uid, userUid,
                                    Domain::ContentNodeState::Skipped, 0.0, "cm:progress:c3");
        QVERIFY(skipped);
        const auto afterThree = useCases.coverage(mapUid, userUid);
        QVERIFY(afterThree);
        QCOMPARE(afterThree.value().coverage, 1.5 / 9.0);
        QVERIFY(afterThree.value().caliberText.find("跳过 1") != std::string::npos);

        // completed 必须 progress=1
        QVERIFY(!update(useCases, nodes.value()[3].uid, userUid,
                        Domain::ContentNodeState::Completed, 0.5, "cm:progress:c4-bad"));

        // 幂等：同一键重复 → Conflict；追加式事件只落一行
        QVERIFY(update(useCases, nodes.value()[4].uid, userUid,
                       Domain::ContentNodeState::Completed, 1.0, "cm:progress:c5"));
        QVERIFY(!update(useCases, nodes.value()[4].uid, userUid,
                        Domain::ContentNodeState::Completed, 1.0, "cm:progress:c5"));
        QSqlQuery events(DatabaseManager::instance().database());
        QVERIFY(events.exec(QStringLiteral(
            "SELECT COUNT(*) FROM progress_events_v4 WHERE idempotency_key='cm:progress:c5'")));
        QVERIFY(events.next());
        QCOMPARE(events.value(0).toInt(), 1);

        // 目标级聚合：含已确认地图（本测试中还有未确认的"草稿目录"应被排除）
        const auto byGoal = useCases.coverageByGoal(goalUid(), userUid);
        QVERIFY(byGoal);
        QVERIFY(byGoal.value().coverage > 0.0);
    }

    void weightedCoverage()
    {
        Infrastructure::SqlContentMapRepository repo(DatabaseManager::instance().database(),
                                                     m_clock);
        Infrastructure::SqlGoalRepository goalsRepo(DatabaseManager::instance().database(),
                                                    m_clock);
        Application::ContentMapUseCases useCases(repo, goalsRepo, m_uids, m_clock);
        const auto userUid = *Domain::Uid::parse(kUserUid);

        // 两个节点权重 1 和 3：完成权重 3 的节点 → 75%，口径为加权
        const auto mapUid = createMap(QStringLiteral("加权目录").toStdString());
        QVERIFY(!mapUid.empty());
        Application::ContentMapUseCases::AddNodesInput addInput;
        Domain::ContentNode light;
        light.title = QStringLiteral("小节").toStdString();
        light.nodeType = "section";
        light.sequenceNo = 0;
        light.weight = 1.0;
        addInput.nodes.push_back(light);
        Domain::ContentNode heavy;
        heavy.title = QStringLiteral("大部头章").toStdString();
        heavy.nodeType = "chapter";
        heavy.sequenceNo = 1;
        heavy.weight = 3.0;
        addInput.nodes.push_back(heavy);
        QVERIFY(useCases.addNodes(mapUid, addInput));
        QVERIFY(useCases.confirmMap(mapUid, 1));

        const auto nodes = useCases.nodesOf(mapUid);
        QVERIFY(nodes);
        QCOMPARE(nodes.value().size(), 2);
        const auto heavyUid = nodes.value()[1].weight > nodes.value()[0].weight
                                  ? nodes.value()[1].uid
                                  : nodes.value()[0].uid;
        QVERIFY(update(useCases, heavyUid, userUid, Domain::ContentNodeState::Completed, 1.0,
                       "cm:progress:weighted"));
        const auto coverage = useCases.coverage(mapUid, userUid);
        QVERIFY(coverage);
        QCOMPARE(coverage.value().coverage, 0.75);
        QVERIFY(coverage.value().caliberText.find("加权") != std::string::npos);
    }

private:
    bool update(Application::ContentMapUseCases &useCases, const Domain::Uid &nodeUid,
                const Domain::Uid &userUid, Domain::ContentNodeState state, double progress,
                const std::string &key)
    {
        Application::ContentMapUseCases::UpdateProgressInput input;
        input.userId = userUid;
        input.state = state;
        input.progress = progress;
        input.idempotencyKey = key;
        return useCases.updateProgress(nodeUid, input).hasValue();
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstContentMap)
#include "tst_content_map.moc"
