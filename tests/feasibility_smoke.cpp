// v1.0 Step 2（全局 Step 15）冒烟测试（design.md 1.12 Step 2）
// 覆盖：FeasibilityCalculator（R 计算与超载判定，阈值注入）/
//       GuardrailService（workload 预警生成、按日幂等、dismiss）/
//       PlanningService::setPlanBasis（FR-C-05 计划依据记录）
// 先验数值为 population_default 占位（Q1：人群先验起步，待研究证据校准）——
// 本测试验证机制正确性，不验证数值合理性。
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSqlQuery>

#include "TestLogging.h"
#include "database/DatabaseManager.h"
#include "database/TaskRepository.h"
#include "feedback/GuardrailService.h"
#include "models/Task.h"
#include "planning/FeasibilityCalculator.h"
#include "planning/PlanningService.h"
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

qint64 makeTask(const QString &date, int planned)
{
    Task t;
    t.title = QStringLiteral("可行性测试任务 %1-%2").arg(date).arg(planned);
    t.dueDate = date;
    t.plannedMinutes = planned;
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

    const QString dbPath = QDir::temp().filePath(QStringLiteral("personos_smoke_feasibility.db"));
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
    FeasibilityCalculator calc;
    GuardrailService guard;
    PlanningService planning;

    const QString d20 = QStringLiteral("2026-09-20");
    const QString d21 = QStringLiteral("2026-09-21");
    const QString d19 = QStringLiteral("2026-09-19");

    // ---- 1. 默认阈值占位参数（Q1：population_default，待证据）----
    check(pmSvc.ensureDefaults(&err), QStringLiteral("ensureDefaults 成功"));
    const auto th = pmSvc.get(QStringLiteral("workload_threshold"));
    check(th && th->value && *th->value == 1.2
              && th->priorSource == QStringLiteral("population_default"),
          QStringLiteral("workload_threshold 占位默认 1.2 / population_default"));

    // ---- 2. R 计算：180 / 360 = 0.5，可行 ----
    makeTask(d20, 60);
    makeTask(d20, 60);
    makeTask(d20, 60);
    {
        const auto r = calc.check(d20, &err);
        check(err.isEmpty() && r.computable && r.plannedMinutes == 180
                  && r.capacityMinutes == 360 && std::abs(r.ratio - 0.5) < 1e-9 && !r.overload,
              QStringLiteral("R = 0.5（180/360），可行"));
        check(!r.explanation.isEmpty() && r.explanation.contains(QStringLiteral("可行")),
              QStringLiteral("explanation 可解释输出非空"));
    }

    // ---- 3. 超载：容量改为 2h → R = 1.5 > 1.2 ----
    setParam(pmSvc, QStringLiteral("time_capacity"), 2.0);
    {
        const auto r = calc.check(d20, &err);
        check(err.isEmpty() && r.capacityMinutes == 120 && std::abs(r.ratio - 1.5) < 1e-9
                  && r.overload,
              QStringLiteral("R = 1.5（180/120），超载"));
    }

    // ---- 4. 边界：R == 阈值不超载（严格大于）----
    // 09-21：144 分钟计划 / 120 分钟容量 = 1.2 == 阈值 → 不超载
    makeTask(d21, 60);
    makeTask(d21, 60);
    makeTask(d21, 24);
    {
        const auto r = calc.check(d21, &err);
        check(err.isEmpty() && std::abs(r.ratio - 1.2) < 1e-9 && !r.overload,
              QStringLiteral("R = 1.2（144/120）边界不超载"));
    }

    // ---- 5. 阈值注入：判定随参数变化（机制验证）----
    setParam(pmSvc, QStringLiteral("workload_threshold"), 1.4);
    check(calc.check(d20, &err).overload, QStringLiteral("阈值 1.4 → R=1.5 仍超载"));
    setParam(pmSvc, QStringLiteral("workload_threshold"), 2.0);
    check(!calc.check(d20, &err).overload, QStringLiteral("阈值 2.0 → R=1.5 不超载"));
    setParam(pmSvc, QStringLiteral("workload_threshold"), 1.2);

    // ---- 6. 无计划任务 → R=0 不超载 ----
    {
        const auto r = calc.check(d19, &err);
        check(err.isEmpty() && r.plannedMinutes == 0 && r.ratio == 0.0 && !r.overload,
              QStringLiteral("无计划任务 → R=0 不超载"));
    }

    // ---- 7. time_capacity 缺失 → 不可计算 + error ----
    {
        QSqlQuery del(DatabaseManager::instance().database());
        del.exec(QStringLiteral("DELETE FROM personal_model_params WHERE key='time_capacity'"));
        const auto r = calc.check(d20, &err);
        check(!r.computable && !err.isEmpty(), QStringLiteral("容量参数缺失 → 不可计算 + error"));
        pmSvc.ensureDefaults(&err); // 恢复默认（seedDefault 只在缺失时写入）
        setParam(pmSvc, QStringLiteral("time_capacity"), 2.0); // 恢复后续预警用例的超载前提
    }

    // ---- 8. Workload Warning 生成（超载：容量 2h，阈值 1.2）----
    {
        const auto w = guard.checkWorkload(d20, &err);
        check(err.isEmpty() && w && w->id > 0 && w->type == QStringLiteral("workload")
                  && !w->rule.isEmpty() && !w->dataSnapshot.isEmpty()
                  && !w->threshold.isEmpty() && !w->suggestion.isEmpty()
                  && w->status == QStringLiteral("active"),
              QStringLiteral("超载 → Workload Warning（字段齐全）"));
        check(w->evidenceRef.contains(QStringLiteral("待研究证据")),
              QStringLiteral("无证据显式标注（NFR-13）"));
    }

    // ---- 9. 按日幂等：重复检查返回同一预警 ----
    {
        const auto w1 = guard.checkWorkload(d20, &err);
        const auto w2 = guard.checkWorkload(d20, &err);
        check(w1 && w2 && w1->id == w2->id, QStringLiteral("同日重复检查幂等（同 id 不新增）"));
    }

    // ---- 10. 可行日不生成预警 ----
    {
        const auto w = guard.checkWorkload(d19, &err);
        check(err.isEmpty() && !w.has_value(), QStringLiteral("可行日 → 无预警"));
    }

    // ---- 11. 不同日期独立预警 ----
    makeTask(d21, 30); // 09-21: 174 分钟 / 120 = 1.45 超载
    {
        const auto w21 = guard.checkWorkload(d21, &err);
        const auto w20 = guard.checkWorkload(d20, &err);
        check(w21 && w20 && w21->id != w20->id, QStringLiteral("不同日期独立预警"));
    }

    // ---- 12. dismiss 处置 ----
    {
        const auto w = guard.checkWorkload(d20, &err);
        check(guard.dismiss(w->id, QStringLiteral("用户已压缩任务"), &err) && err.isEmpty(),
              QStringLiteral("dismiss 成功"));
        check(guard.active(d20).isEmpty(), QStringLiteral("dismiss 后当日无 active 预警"));
        check(!guard.dismiss(99999, QStringLiteral("不存在"), &err) && !err.isEmpty(),
              QStringLiteral("不存在的预警拒绝 dismiss"));
    }

    // ---- 13. 计划依据记录（FR-C-05）----
    {
        check(planning.setPlanBasis(d20,
                                    QStringLiteral("目标：数学；容量 360 分钟；约束：下午有课"),
                                    &err)
                  && err.isEmpty(),
              QStringLiteral("计划依据写入成功"));
        const auto plan = planning.dailyPlan(d20);
        check(plan && plan->note.contains(QStringLiteral("约束：下午有课")),
              QStringLiteral("计划依据读回一致（plans.note）"));
        check(planning.setPlanBasis(d20, QStringLiteral("覆盖后的依据"), &err),
              QStringLiteral("计划依据再次写入（覆盖）"));
        check(planning.dailyPlan(d20)->note == QStringLiteral("覆盖后的依据"),
              QStringLiteral("覆盖生效"));
        check(!planning.setPlanBasis(d20, QStringLiteral("   "), &err) && !err.isEmpty(),
              QStringLiteral("空依据被拒绝"));
    }

    // ---- 14. 日期校验 ----
    {
        check(!calc.check(QStringLiteral("bad-date"), &err).computable && !err.isEmpty(),
              QStringLiteral("Feasibility 非法日期被拒绝"));
        const auto w = guard.checkWorkload(QStringLiteral("bad-date"), &err);
        check(!w.has_value() && !err.isEmpty(), QStringLiteral("Guardrail 非法日期被拒绝"));
    }

    // ---- 汇总 ----
    qInfo().noquote() << (g_failures == 0
                              ? QStringLiteral("===== SMOKE PASS =====")
                              : QStringLiteral("===== SMOKE FAIL (%1) =====").arg(g_failures));
    return g_failures == 0 ? 0 : 1;
}
