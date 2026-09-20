// Step 13 MVP 闭环验收测试（Qt Test，README 3.14）
// 按 2.7.21 验证标准：记录现实 → 描述状态 → 生成计划 → 执行 → 记录结果 →
// 复盘 → 形成下一轮计划，以及 Evolution 变更链。
#include <QtTest>

#include <QDate>

#include "TestLogging.h"
#include "database/ChangeLogRepository.h"
#include "database/DatabaseManager.h"
#include "database/EventRepository.h"
#include "database/TaskRepository.h"
#include "evolution/EvolutionService.h"
#include "execution/ExecutionService.h"
#include "goals/CoreAndGoalService.h"
#include "planning/PlanningService.h"
#include "review/ReviewService.h"
#include "state/StateService.h"

using namespace PersonOS;

class TstMvpLoop : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Test::installMessageHandler();

        m_dbPath = QDir::temp().filePath(QStringLiteral("personos_tst_mvp.db"));
        QFile::remove(m_dbPath);
        QFile::remove(m_dbPath + QStringLiteral("-wal"));
        QFile::remove(m_dbPath + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_dbPath.toUtf8());

        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
    }

    void fullLoop()
    {
        CoreAndGoalService goals;
        PlanningService planning;
        ExecutionService execution;
        StateService states;
        ReviewService reviews;
        QString err;

        // ---- 1. 目标 ----
        Goal g;
        g.level = QStringLiteral("weekly");
        g.title = QStringLiteral("本周完成高数第三章");
        const qint64 goalId = goals.createGoal(g, &err);
        QVERIFY2(goalId > 0, qPrintable(err));

        // ---- 2. 计划（昨天，用于验证补录机制）----
        const QString yesterday =
            QDate::currentDate().addDays(-1).toString(QStringLiteral("yyyy-MM-dd"));
        Task t1;
        t1.title = QStringLiteral("学习高数");
        t1.plannedMinutes = 120;
        t1.goalId = goalId;
        const qint64 task1 = planning.addTaskToPlan(yesterday, t1, &err);
        QVERIFY2(task1 > 0, qPrintable(err));
        Task t2;
        t2.title = QStringLiteral("背单词");
        t2.plannedMinutes = 30;
        const qint64 task2 = planning.addTaskToPlan(yesterday, t2, &err);
        Task t3;
        t3.title = QStringLiteral("跑步");
        t3.plannedMinutes = 40;
        const qint64 task3 = planning.addTaskToPlan(yesterday, t3, &err);
        QVERIFY(task2 > 0 && task3 > 0);

        // ---- 3. 执行（2.5.2：每次流转追加事件）----
        QVERIFY2(execution.startTask(task1, &err), qPrintable(err));
        QVERIFY2(execution.completeTask(task1, 90, &err), qPrintable(err));
        QVERIFY2(execution.skipTask(task2, QStringLiteral("临时有事"), &err), qPrintable(err));
        QVERIFY2(execution.cancelTask(task3, QStringLiteral("下雨"), &err), qPrintable(err));

        // ---- 4. 事件流（行动日 = 今天）----
        const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
        const auto events = EventRepository().getByDate(today);
        QCOMPARE(events.size(), 4); // started + completed + skipped + cancelled

        // ---- 5. 状态 ----
        StateSnapshot s;
        s.date = yesterday;
        s.sleepHours = 7.5;
        s.energy = 3;
        s.focus = 4;
        s.mood = 2;
        QVERIFY2(states.record(s, &err), qPrintable(err));

        // ---- 6. 补录检查：有计划未复盘 → 在列表 ----
        QVERIFY(reviews.pendingReviewDates(7).contains(yesterday));

        // ---- 7. 复盘草案（2.5.6 计划 vs 实际）----
        const Review draft = reviews.draftDailyReview(yesterday);
        QVERIFY(draft.summary.contains(QStringLiteral("计划任务 3")));
        QVERIFY(draft.summary.contains(QStringLiteral("完成率: 33%")));
        QVERIFY(draft.summary.contains(QStringLiteral("时长偏差: -100")));
        QVERIFY(draft.summary.contains(QStringLiteral("睡眠 7.5h")));

        // ---- 8. 保存复盘 → 形成下一轮（next_actions）----
        Review saved = draft;
        saved.problems = QStringLiteral("下午执行率低");
        saved.causes = QStringLiteral("睡眠不足");
        saved.nextActions = QStringLiteral("今晚 23 点前睡觉，明日任务减量 30%");
        QVERIFY2(reviews.saveDailyReview(saved, &err), qPrintable(err));
        QVERIFY(!reviews.pendingReviewDates(7).contains(yesterday));

        // ---- 9. 关闭计划 ----
        QVERIFY2(planning.closeDailyPlan(yesterday, &err), qPrintable(err));
        QCOMPARE(planning.dailyPlan(yesterday)->status, QStringLiteral("closed"));

        // ---- 10. 任务→目标关联（FR-010）----
        QCOMPARE(TaskRepository().getById(task1)->goalId, goalId);
    }

    void evolutionChain()
    {
        EvolutionService evo;
        QString err;

        Proposal p;
        p.targetType = QStringLiteral("strategy");
        p.currentValue = QStringLiteral("每天固定 5 小时学习");
        p.proposedValue = QStringLiteral("每天 4 小时核心学习 + 弹性任务");
        p.reason = QStringLiteral("过去 21 天完成率持续低于预期");
        p.evidence = QStringLiteral("完成率均值 65%");
        const qint64 id = evo.propose(p, &err);
        QVERIFY2(id > 0, qPrintable(err));
        QVERIFY2(evo.decide(id, true, &err), qPrintable(err));
        QVERIFY2(evo.applyApproved(id, &err), qPrintable(err));

        const auto version = evo.currentVersion();
        QVERIFY(version);
        QCOMPARE(version->versionNumber, QStringLiteral("v0.1"));

        const auto logs = ChangeLogRepository().getByTarget(QStringLiteral("strategy"), 0);
        QCOMPARE(logs.size(), 1);
        QCOMPARE(logs.front().proposalId, id);
        QCOMPARE(logs.front().versionId, version->id);
    }

private:
    QString m_dbPath;
};

QTEST_GUILESS_MAIN(TstMvpLoop)
#include "tst_mvp_loop.moc"
