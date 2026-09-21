// Step 10 冒烟测试（控制台程序，README 3.11）
// 覆盖：ReviewRepository / ReviewService（draft/save/pendingReviewDates）
// 使用独立测试库（PERSONOS_DB_PATH 指向临时文件），不污染真实数据。
#include <QCoreApplication>
#include <QDate>
#include <QDebug>
#include <QDir>
#include <QFile>

#include "TestLogging.h"
#include "database/DatabaseManager.h"
#include "execution/ExecutionService.h"
#include "planning/PlanningService.h"
#include "review/ReviewService.h"
#include "state/StateService.h"

using namespace PersonOS;

namespace {

int g_failures = 0;

void check(bool ok, const QString &name)
{
    if (ok)
        qInfo().noquote() << QStringLiteral("[PASS] %1").arg(name);
    else {
        qInfo().noquote() << QStringLiteral("[FAIL] %1").arg(name);
        ++g_failures;
    }
}

} // namespace

int main(int argc, char *argv[])
{
    Test::installMessageHandler();

    QCoreApplication app(argc, argv);

    // 独立测试库
    const QString dbPath = QDir::temp().filePath(QStringLiteral("personos_smoke_review.db"));
    QFile::remove(dbPath);
    QFile::remove(dbPath + QStringLiteral("-wal"));
    QFile::remove(dbPath + QStringLiteral("-shm"));
    qputenv("PERSONOS_DB_PATH", dbPath.toUtf8());

    if (!DatabaseManager::instance().open()) {
        qInfo().noquote() << QStringLiteral("[FAIL] 测试库打开失败: %1")
                                 .arg(DatabaseManager::instance().lastError());
        return 1;
    }

    PlanningService planning;
    ExecutionService execution;
    StateService states;
    ReviewService reviews;
    QString err;
    const QString date = QStringLiteral("2026-09-20");

    // ---- 0. 准备：3 个任务 + 状态快照 ----
    Task t1;
    t1.title = QStringLiteral("学习高数");
    t1.plannedMinutes = 120;
    const qint64 task1 = planning.addTaskToPlan(date, t1, &err);

    Task t2;
    t2.title = QStringLiteral("背单词");
    t2.plannedMinutes = 30;
    const qint64 task2 = planning.addTaskToPlan(date, t2, &err);

    Task t3;
    t3.title = QStringLiteral("跑步");
    t3.plannedMinutes = 40;
    const qint64 task3 = planning.addTaskToPlan(date, t3, &err);

    check(task1 > 0 && task2 > 0 && task3 > 0, QStringLiteral("准备：创建 3 个任务"));

    execution.startTask(task1, &err);
    execution.completeTask(task1, 90, &err);
    execution.skipTask(task2, QStringLiteral("临时有事"), &err);
    execution.cancelTask(task3, QStringLiteral("下雨"), &err);

    StateSnapshot s;
    s.date = date;
    s.sleepHours = 7.5;
    s.energy = 3;
    s.focus = 4;
    s.mood = 2;
    check(states.record(s, &err), QStringLiteral("准备：记录状态快照"));

    // ---- 1. 起草复盘：计划 vs 实际 ----
    const Review draft = reviews.draftDailyReview(date);
    check(draft.id == 0 && draft.reviewType == QStringLiteral("daily") && draft.periodStart == date,
          QStringLiteral("草案元信息正确（未落库）"));
    check(draft.summary.contains(QStringLiteral("计划任务 3")) && draft.summary.contains(QStringLiteral("完成 1"))
              && draft.summary.contains(QStringLiteral("跳过 1")) && draft.summary.contains(QStringLiteral("取消 1")),
          QStringLiteral("草案含任务统计（3/1/1/1）"));
    check(draft.summary.contains(QStringLiteral("完成率: 33%")), QStringLiteral("草案含完成率 33%"));
    check(draft.summary.contains(QStringLiteral("时长偏差: -100")), // 计划 190 / 实际 90
          QStringLiteral("草案含时长偏差 -100"));
    check(draft.summary.contains(QStringLiteral("睡眠 7.5h")) && draft.summary.contains(QStringLiteral("精力 3")),
          QStringLiteral("草案含当日状态摘要"));

    // ---- 2. 无任务日期草案 ----
    const Review empty = reviews.draftDailyReview(QStringLiteral("2026-09-15"));
    check(empty.summary.contains(QStringLiteral("无计划任务")), QStringLiteral("无任务日期草案"));

    // ---- 3. 保存复盘 ----
    Review saved = draft;
    saved.problems = QStringLiteral("下午执行率低");
    saved.causes = QStringLiteral("睡眠不足");
    saved.nextActions = QStringLiteral("明晚早睡");
    check(reviews.saveDailyReview(saved, &err) && err.isEmpty(), QStringLiteral("保存日复盘"));
    const auto back = reviews.dailyReview(date);
    check(back && back->problems == saved.problems && back->nextActions == saved.nextActions,
          QStringLiteral("读回复盘内容一致"));

    // ---- 4. 同日期再保存 = upsert 不新增（FR-004 修正记录）----
    saved.summary = saved.summary + QStringLiteral("\n（修正）补充一行");
    check(reviews.saveDailyReview(saved, &err) && reviews.dailyReview(date)->summary.contains(QStringLiteral("（修正）")),
          QStringLiteral("同日期 upsert 修正"));

    // ---- 5. 保存校验 ----
    Review bad = draft;
    bad.summary.clear();
    check(!reviews.saveDailyReview(bad, &err) && err.contains(QStringLiteral("总结")),
          QStringLiteral("空总结被拒绝"));
    bad = draft;
    bad.periodStart = QStringLiteral("2026-13-99");
    check(!reviews.saveDailyReview(bad, &err) && !err.isEmpty(), QStringLiteral("非法日期被拒绝"));
    bad = draft;
    bad.reviewType = QStringLiteral("weekly");
    check(!reviews.saveDailyReview(bad, &err) && err.contains(QStringLiteral("daily")),
          QStringLiteral("非日复盘被拒绝"));

    // ---- 6. 补录检查（3.0.3 循环触发时机）----
    const QString yesterday =
        QDate::currentDate().addDays(-1).toString(QStringLiteral("yyyy-MM-dd"));
    check(!reviews.pendingReviewDates(7).contains(yesterday),
          QStringLiteral("昨天无计划 → 不在补录列表"));

    planning.ensureDailyPlan(yesterday, &err); // 昨天有计划但未复盘
    check(reviews.pendingReviewDates(7).contains(yesterday),
          QStringLiteral("昨天有计划未复盘 → 在补录列表"));

    Review yReview;
    yReview.reviewType = QStringLiteral("daily");
    yReview.periodStart = yesterday;
    yReview.summary = QStringLiteral("补录：昨天计划完成");
    reviews.saveDailyReview(yReview, &err);
    check(!reviews.pendingReviewDates(7).contains(yesterday),
          QStringLiteral("补录完成后移出列表"));

    // ---- 汇总 ----
    qInfo().noquote() << (g_failures == 0
                              ? QStringLiteral("===== SMOKE PASS =====")
                              : QStringLiteral("===== SMOKE FAIL (%1) =====").arg(g_failures));
    return g_failures == 0 ? 0 : 1;
}
