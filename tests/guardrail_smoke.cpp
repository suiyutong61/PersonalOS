// v1.0 Step 3（全局 Step 16）冒烟测试（design.md 1.12 Step 3）
// 覆盖：GuardrailService::checkStall（下界触发 Policy：E_7d / 连续无完成）/
//       Recovery 建议 + 多因素诊断清单 + 禁止"懒惰"归因断言 /
//       Diagnostic 升级（21 天 ≥3 次）/ 按日幂等 / 参数注入 / dismiss
// 阈值均为 population_default 占位（待研究证据校准），本测试验证机制正确性。
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>

#include "TestLogging.h"
#include "database/DatabaseManager.h"
#include "database/ReviewRepository.h"
#include "database/TaskRepository.h"
#include "execution/MetricsService.h"
#include "feedback/GuardrailService.h"
#include "models/Review.h"
#include "models/Task.h"
#include "state/PersonalModelService.h"

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

qint64 makeTask(const QString &date, int planned, int actual, const QString &status)
{
    Task t;
    t.title = QStringLiteral("下界测试任务 %1").arg(date);
    t.dueDate = date;
    t.plannedMinutes = planned;
    t.status = status;
    if (actual > 0)
        t.actualMinutes = actual;
    return TaskRepository().create(t);
}

void setParam(PersonalModelService &svc, const QString &key, double value)
{
    PersonalModelParam p;
    p.key = key;
    p.value = value;
    p.priorSource = QStringLiteral("user_input");
    QString err;
    svc.set(p, &err);
}

} // namespace

int main(int argc, char *argv[])
{
    Test::installMessageHandler();

    QCoreApplication app(argc, argv);

    const QString dbPath = QDir::temp().filePath(QStringLiteral("personos_smoke_guardrail.db"));
    QFile::remove(dbPath);
    QFile::remove(dbPath + QStringLiteral("-wal"));
    QFile::remove(dbPath + QStringLiteral("-shm"));
    qputenv("PERSONOS_DB_PATH", dbPath.toUtf8());

    if (!DatabaseManager::instance().open()) {
        qInfo().noquote() << QStringLiteral("[FAIL] 测试库打开失败: %1")
                                 .arg(DatabaseManager::instance().lastError());
        return 1;
    }

    QString err;
    PersonalModelService pmSvc;
    GuardrailService guard;
    MetricsService metrics;

    // ---- 1. 下界占位参数 ----
    check(pmSvc.ensureDefaults(&err), QStringLiteral("ensureDefaults 成功"));
    {
        const auto th = pmSvc.get(QStringLiteral("stall_threshold"));
        const auto qd = pmSvc.get(QStringLiteral("stall_quiet_days"));
        check(th && th->value && *th->value == 0.4 && qd && qd->value && *qd->value == 3.0
                  && th->priorSource == QStringLiteral("population_default"),
              QStringLiteral("stall_threshold=0.4 / stall_quiet_days=3（占位）"));
    }

    // ---- 2. Range A：E_7d=0.3 < 0.4 且样本 5 天 → 规则1 触发 ----
    // 09-10..09-14 每天 planned 100 / actual 30（completed）
    for (int d = 10; d <= 14; ++d)
        makeTask(QStringLiteral("2026-09-%1").arg(d, 2, 10, QLatin1Char('0')), 100, 30,
                 QStringLiteral("completed"));

    // ---- 3. 反刍信号（09-14）：3 条历史复盘 + 当日长复盘，E_day=0.3<0.5 → 1.0 ----
    {
        const QString medium = QStringLiteral("今天整体正常，按计划完成。");
        ReviewRepository().upsert(
            [&] { Review r; r.periodStart = QStringLiteral("2026-09-08"); r.summary = medium; return r; }());
        ReviewRepository().upsert(
            [&] { Review r; r.periodStart = QStringLiteral("2026-09-09"); r.summary = medium; return r; }());
        ReviewRepository().upsert(
            [&] { Review r; r.periodStart = QStringLiteral("2026-09-10"); r.summary = medium; return r; }());
        Review r;
        r.periodStart = QStringLiteral("2026-09-14");
        r.summary = QStringLiteral("为什么又没学完？今天又刷了很久手机，越想越焦虑，"
                                   "复盘也写不出来，这周已经第三次了，我到底该怎么办，"
                                   "是不是我这个人就是没有执行力，是不是目标定得太高了……");
        ReviewRepository().upsert(r);
        check(metrics.computeDaily(QStringLiteral("2026-09-14"), &err) && err.isEmpty(),
              QStringLiteral("computeDaily(09-14) 成功"));
    }

    // ---- 4. checkStall(09-14)：规则1 触发，字段与纪律断言 ----
    {
        const auto w = guard.checkStall(QStringLiteral("2026-09-14"), &err);
        check(err.isEmpty() && w && w->id > 0 && w->type == QStringLiteral("stall")
                  && w->rule.contains(QStringLiteral("stall_threshold"))
                  && w->status == QStringLiteral("active"),
              QStringLiteral("E_7d 低 → Stall Warning（规则1）"));
        check(w && w->suggestion.contains(QStringLiteral("Recovery 建议"))
                  && w->suggestion.contains(QStringLiteral("多因素诊断清单"))
                  && w->suggestion.contains(QStringLiteral("禁止归因")),
              QStringLiteral("建议含 Recovery + 多因素诊断 + 禁止归因声明"));
        check(w && w->dataSnapshot.contains(QStringLiteral("rumination=1")),
              QStringLiteral("data_snapshot 含反刍信号"));
        check(w && w->evidenceRef.contains(QStringLiteral("待研究证据")),
              QStringLiteral("无证据显式标注（NFR-13）"));
    }

    // ---- 5. 按日幂等 ----
    {
        const auto w1 = guard.checkStall(QStringLiteral("2026-09-14"), &err);
        const auto w2 = guard.checkStall(QStringLiteral("2026-09-14"), &err);
        check(w1 && w2 && w1->id == w2->id, QStringLiteral("同日重复检查幂等"));
    }

    // ---- 6. Range B：连续 3 天有计划但无完成 → 规则2 触发 ----
    // 09-13..15 完成；09-16..18 有计划无完成（E 窗口化 ≥ 0.4，规则1 不触发）
    for (int d = 13; d <= 15; ++d)
        makeTask(QStringLiteral("2026-09-%1").arg(d, 2, 10, QLatin1Char('0')), 60, 60,
                 QStringLiteral("completed"));
    for (int d = 16; d <= 18; ++d)
        makeTask(QStringLiteral("2026-09-%1").arg(d, 2, 10, QLatin1Char('0')), 60, 0,
                 QStringLiteral("planned"));
    {
        const auto w = guard.checkStall(QStringLiteral("2026-09-18"), &err);
        check(err.isEmpty() && w && w->rule.contains(QStringLiteral("连续 3 天")),
              QStringLiteral("连续无完成 → Stall Warning（规则2）"));
    }

    // ---- 7. Range C：执行正常 → 不触发 ----
    // 09-01..03 每天 planned 50 / actual 50
    for (int d = 1; d <= 3; ++d)
        makeTask(QStringLiteral("2026-09-%1").arg(d, 2, 10, QLatin1Char('0')), 50, 50,
                 QStringLiteral("completed"));
    {
        const auto w = guard.checkStall(QStringLiteral("2026-09-03"), &err);
        check(err.isEmpty() && !w.has_value(), QStringLiteral("执行正常 → 无预警"));
    }

    // ---- 8. 窗口无计划任务 → 不判定 ----
    {
        const auto w = guard.checkStall(QStringLiteral("2026-08-01"), &err);
        check(err.isEmpty() && !w.has_value(), QStringLiteral("无计划任务窗口 → 不判定"));
    }

    // ---- 9. 参数注入：阈值变化 → 判定变化（新日期避免幂等短路）----
    // Range D：09-20..22 每天 planned 100 / actual 35 → E=0.35
    for (int d = 20; d <= 22; ++d)
        makeTask(QStringLiteral("2026-09-%1").arg(d, 2, 10, QLatin1Char('0')), 100, 35,
                 QStringLiteral("completed"));
    {
        setParam(pmSvc, QStringLiteral("stall_threshold"), 0.2);
        check(!guard.checkStall(QStringLiteral("2026-09-22"), &err).has_value(),
              QStringLiteral("阈值 0.2 → E=0.35 不触发"));
        setParam(pmSvc, QStringLiteral("stall_threshold"), 0.4);
        const auto w = guard.checkStall(QStringLiteral("2026-09-22"), &err);
        check(w.has_value(), QStringLiteral("阈值 0.4 → E=0.35 触发"));
        check(guard.dismiss(w->id, QStringLiteral("用户已处理"), &err) && err.isEmpty(),
              QStringLiteral("stall 预警 dismiss 成功"));
    }

    // ---- 10. dismiss 不存在 → false + error ----
    check(!guard.dismiss(99999, QStringLiteral("不存在"), &err) && !err.isEmpty(),
          QStringLiteral("不存在的预警拒绝 dismiss"));

    // ---- 11. Diagnostic 升级：21 天内已 3 次 stall（09-14/09-18/09-22）----
    // Range E：09-23..24 每天 planned 100 / actual 30 → 规则1 触发 → 建议含 Diagnostic
    for (int d = 23; d <= 24; ++d)
        makeTask(QStringLiteral("2026-09-%1").arg(d, 2, 10, QLatin1Char('0')), 100, 30,
                 QStringLiteral("completed"));
    {
        const auto w = guard.checkStall(QStringLiteral("2026-09-24"), &err);
        check(err.isEmpty() && w && w->suggestion.contains(QStringLiteral("Diagnostic 阶段")),
              QStringLiteral("第 4 次 stall → 建议含 Diagnostic 升级"));
    }

    // ---- 12. 非法日期 ----
    {
        const auto w = guard.checkStall(QStringLiteral("bad-date"), &err);
        check(!w.has_value() && !err.isEmpty(), QStringLiteral("非法日期被拒绝"));
    }

    // ---- 汇总 ----
    qInfo().noquote() << (g_failures == 0
                              ? QStringLiteral("===== SMOKE PASS =====")
                              : QStringLiteral("===== SMOKE FAIL (%1) =====").arg(g_failures));
    return g_failures == 0 ? 0 : 1;
}
