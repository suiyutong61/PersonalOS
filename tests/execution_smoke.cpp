// Step 8 冒烟测试（控制台程序，README 3.9）
// 覆盖：TaskRepository / EventRepository(append) / ExecutionService /
//       PlanningService::addTaskToPlan
// 使用独立测试库（PERSONOS_DB_PATH 指向临时文件），不污染真实数据。
#include <QCoreApplication>
#include <QDate>
#include <QDebug>
#include <QDir>
#include <QFile>

#include "TestLogging.h"
#include "database/DatabaseManager.h"
#include "execution/ExecutionService.h"
#include "goals/CoreAndGoalService.h"
#include "planning/PlanningService.h"

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
    const QString dbPath = QDir::temp().filePath(QStringLiteral("personos_smoke_execution.db"));
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
    CoreAndGoalService goals;
    QString err;
    const QString date = QStringLiteral("2026-09-20");
    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));

    // ---- 0. 准备目标（任务→目标关联）----
    Goal g;
    g.level = QStringLiteral("weekly");
    g.title = QStringLiteral("本周学习数学");
    const qint64 goalId = goals.createGoal(g, &err);
    check(goalId > 0, QStringLiteral("准备：创建目标"));

    // ---- 1. addTaskToPlan：自动创建计划并挂入任务 ----
    Task t1;
    t1.title = QStringLiteral("学习高数 2 小时");
    t1.plannedMinutes = 120;
    t1.goalId = goalId;
    const qint64 task1 = planning.addTaskToPlan(date, t1, &err);
    check(task1 > 0 && err.isEmpty(), QStringLiteral("addTaskToPlan 创建任务"));

    const auto plan = planning.dailyPlan(date);
    check(plan && plan->id > 0, QStringLiteral("计划被自动创建"));

    Task t2;
    t2.title = QStringLiteral("背单词 30 分钟");
    t2.plannedMinutes = 30;
    t2.sortOrder = 1;
    const qint64 task2 = planning.addTaskToPlan(date, t2, &err);
    check(task2 > 0, QStringLiteral("第二个任务"));

    // ---- 2. 按日期查询任务 ----
    TaskRepository taskRepo;
    check(taskRepo.getByDate(date).size() == 2, QStringLiteral("当日任务共 2 条"));

    // ---- 3. 空标题任务拒绝 ----
    Task bad;
    bad.title = QStringLiteral(" ");
    check(planning.addTaskToPlan(date, bad, &err) == 0 && !err.isEmpty(),
          QStringLiteral("空标题任务被拒绝"));

    // ---- 4. 状态流转：start ----
    check(execution.startTask(task1, &err) && err.isEmpty(), QStringLiteral("startTask"));
    check(taskRepo.getById(task1)->status == QStringLiteral("started"),
          QStringLiteral("状态已为 started"));

    // ---- 5. 状态流转：complete ----
    check(execution.completeTask(task1, 90, &err), QStringLiteral("completeTask(90 分钟)"));
    const auto done = taskRepo.getById(task1);
    check(done->status == QStringLiteral("completed") && *done->actualMinutes == 90
              && !done->completedAt.isEmpty(),
          QStringLiteral("completed 且 actual=90、completedAt 已填"));

    // ---- 6. 终态保护：重复 complete / start ----
    check(!execution.completeTask(task1, 60, &err) && !err.isEmpty(),
          QStringLiteral("已终结任务不能重复完成"));
    check(!execution.startTask(task1, &err) && !err.isEmpty(),
          QStringLiteral("已终结任务不能重新开始"));

    // ---- 7. skip（带原因）----
    check(execution.skipTask(task2, QStringLiteral("临时有事"), &err) && err.isEmpty(),
          QStringLiteral("skipTask"));
    check(taskRepo.getById(task2)->status == QStringLiteral("skipped"),
          QStringLiteral("状态已为 skipped"));

    // ---- 8. 不存在的任务 ----
    check(!execution.startTask(999999, &err) && err.contains(QStringLiteral("不存在")),
          QStringLiteral("流转不存在的任务被拒绝"));

    // ---- 9. 负数实际时长 ----
    Task t3;
    t3.title = QStringLiteral("第三个任务");
    const qint64 task3 = planning.addTaskToPlan(date, t3, &err);
    check(task3 > 0 && !execution.completeTask(task3, -5, &err) && !err.isEmpty(),
          QStringLiteral("负数实际时长被拒绝"));

    // ---- 10. cancel ----
    check(execution.cancelTask(task3, QStringLiteral("不再需要"), &err),
          QStringLiteral("cancelTask"));

    // ---- 11. 事件流：每次流转追加一条（2.5.2）----
    EventRepository eventRepo;
    const auto events = eventRepo.getByDate(today);
    // task1: started + completed；task2: skipped；task3: cancelled → 共 4 条
    check(events.size() == 4, QStringLiteral("状态流转共追加 4 条事件"));

    // ---- 12. 事件字段与关联 ----
    const Event &lastEvent = events.back();
    check(lastEvent.taskId > 0 && !lastEvent.occurredAt.isEmpty() && lastEvent.date == today,
          QStringLiteral("事件字段完整（taskId/occurredAt/date）"));

    // ---- 13. updateTask 不允许改状态（3.3.2 纪律）----
    Task edit = *taskRepo.getById(task3);
    edit.title = QStringLiteral("改名");
    edit.status = QStringLiteral("started"); // 非法：绕过状态流转
    check(!execution.updateTask(edit, &err) && err.contains(QStringLiteral("状态")),
          QStringLiteral("updateTask 改状态被拒绝"));
    edit.status = QStringLiteral("cancelled");
    edit.title = QStringLiteral("第三个任务（改名）");
    check(execution.updateTask(edit, &err) && taskRepo.getById(task3)->title.contains(QStringLiteral("改名")),
          QStringLiteral("updateTask 改名成功"));

    // ---- 14. 任务→目标关联持久化（FR-010）----
    check(taskRepo.getById(task1)->goalId == goalId, QStringLiteral("任务→目标关联持久化"));

    // ---- 汇总 ----
    qInfo().noquote() << (g_failures == 0
                              ? QStringLiteral("===== SMOKE PASS =====")
                              : QStringLiteral("===== SMOKE FAIL (%1) =====").arg(g_failures));
    return g_failures == 0 ? 0 : 1;
}
