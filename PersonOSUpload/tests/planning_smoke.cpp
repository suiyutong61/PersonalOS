// Step 7 冒烟测试（控制台程序，README 3.8）
// 覆盖：PlanRepository / PlanningService
// 使用独立测试库（PERSONOS_DB_PATH 指向临时文件），不污染真实数据。
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>

#include "TestLogging.h"
#include "database/DatabaseManager.h"
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
    const QString dbPath = QDir::temp().filePath(QStringLiteral("personos_smoke_planning.db"));
    QFile::remove(dbPath);
    QFile::remove(dbPath + QStringLiteral("-wal"));
    QFile::remove(dbPath + QStringLiteral("-shm"));
    qputenv("PERSONOS_DB_PATH", dbPath.toUtf8());

    if (!DatabaseManager::instance().open()) {
        qInfo().noquote() << QStringLiteral("[FAIL] 测试库打开失败: %1")
                                 .arg(DatabaseManager::instance().lastError());
        return 1;
    }

    PlanningService svc;
    QString err;
    const QString date1 = QStringLiteral("2026-09-20");
    const QString date2 = QStringLiteral("2026-09-21");

    // ---- 1. 初始无计划 ----
    check(!svc.dailyPlan(date1).has_value(), QStringLiteral("初始无日计划"));

    // ---- 2. ensureDailyPlan 创建 ----
    const Plan p1 = svc.ensureDailyPlan(date1, &err);
    check(p1.id > 0 && err.isEmpty(), QStringLiteral("创建日计划"));
    check(p1.periodType == QStringLiteral("daily") && p1.periodStart == date1
              && p1.status == QStringLiteral("active"),
          QStringLiteral("计划字段正确（daily/日期/active）"));

    // ---- 3. ensureDailyPlan 幂等：同一天不重复创建 ----
    const Plan p1b = svc.ensureDailyPlan(date1, &err);
    check(p1b.id == p1.id, QStringLiteral("同日 ensure 返回同一计划"));

    // ---- 4. 不同日期相互独立 ----
    const Plan p2 = svc.ensureDailyPlan(date2, &err);
    check(p2.id > 0 && p2.id != p1.id, QStringLiteral("不同日期创建独立计划"));

    // ---- 5. 非法日期拒绝 ----
    const Plan bad1 = svc.ensureDailyPlan(QStringLiteral("2026-13-99"), &err);
    check(bad1.id == 0 && !err.isEmpty(), QStringLiteral("非法日期被拒绝"));

    // ---- 6. 空日期拒绝 ----
    const Plan bad2 = svc.ensureDailyPlan(QStringLiteral(""), &err);
    check(bad2.id == 0 && !err.isEmpty(), QStringLiteral("空日期被拒绝"));

    // ---- 7. 关闭计划 ----
    check(svc.closeDailyPlan(date1, &err) && err.isEmpty(), QStringLiteral("关闭日计划"));
    check(svc.dailyPlan(date1)->status == QStringLiteral("closed"), QStringLiteral("状态已为 closed"));

    // ---- 8. 关闭不存在的计划 ----
    check(!svc.closeDailyPlan(QStringLiteral("2026-09-22"), &err) && !err.isEmpty(),
          QStringLiteral("关闭不存在计划被拒绝"));

    // ---- 9. 关闭后的计划 ensure 仍返回原计划（不新建）----
    const Plan p1c = svc.ensureDailyPlan(date1, &err);
    check(p1c.id == p1.id, QStringLiteral("closed 计划 ensure 不新建"));

    // ---- 汇总 ----
    qInfo().noquote() << (g_failures == 0
                              ? QStringLiteral("===== SMOKE PASS =====")
                              : QStringLiteral("===== SMOKE FAIL (%1) =====").arg(g_failures));
    return g_failures == 0 ? 0 : 1;
}
