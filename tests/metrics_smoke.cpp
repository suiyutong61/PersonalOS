// v1.0 Step 1（全局 Step 14）冒烟测试（design.md 1.12 Step 1）
// 覆盖：Migration v2 / PersonalModelService（默认先验 + 读写）/
//       CalibrationRepository（append-only）/ MetricRepository /
//       MetricsService（E、监控频率、偏差基线、反刍信号、computeDaily）/
//       Task 新字段（trigger/task_type）/ Proposal basis_types
// 使用独立测试库（PERSONOS_DB_PATH 指向临时文件），不污染真实数据。
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>

#include "TestLogging.h"
#include "database/CalibrationRepository.h"
#include "database/DatabaseManager.h"
#include "database/Migrations.h"
#include "database/EventRepository.h"
#include "database/MetricRepository.h"
#include "database/ProposalRepository.h"
#include "database/StateRepository.h"
#include "database/TaskRepository.h"
#include "execution/MetricsService.h"
#include "models/CalibrationRecord.h"
#include "models/Event.h"
#include "models/Proposal.h"
#include "models/Review.h"
#include "models/StateSnapshot.h"
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

bool hasMetric(const QString &date, const QString &key)
{
    const auto all = MetricRepository().getByDate(date);
    for (const Metric &m : all)
        if (m.key == key)
            return true;
    return false;
}

// 造一个任务（planned/actual/status 可指定），返回 id
qint64 makeTask(const QString &date, int planned, int actual, const QString &status)
{
    Task t;
    t.title = QStringLiteral("测试任务 %1").arg(date);
    t.dueDate = date;
    t.plannedMinutes = planned;
    t.status = status;
    if (actual > 0)
        t.actualMinutes = actual;
    return TaskRepository().create(t);
}

Review makeReview(const QString &date, const QString &text)
{
    Review r;
    r.reviewType = QStringLiteral("daily");
    r.periodStart = date;
    r.summary = text;
    return r;
}

} // namespace

int main(int argc, char *argv[])
{
    Test::installMessageHandler();

    QCoreApplication app(argc, argv);

    // 独立测试库
    const QString dbPath = QDir::temp().filePath(QStringLiteral("personos_smoke_metrics.db"));
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
    MetricsService ms;

    // ---- 1. Migration v2：schema 版本与新增列 ----
    check(DatabaseManager::instance().schemaVersion() == Migrations::kLatestVersion,
          QStringLiteral("schema 已升级到最新版本"));

    // ---- 2. 默认先验参数（Q1：人群先验起步）----
    check(pmSvc.ensureDefaults(&err) && err.isEmpty(), QStringLiteral("ensureDefaults 成功"));
    const auto cap = pmSvc.get(QStringLiteral("time_capacity"));
    check(cap && cap->value && *cap->value == 6.0
              && cap->priorSource == QStringLiteral("population_default"),
          QStringLiteral("time_capacity 默认 6.0 / population_default"));
    const auto rhythm = pmSvc.get(QStringLiteral("energy_rhythm"));
    check(rhythm && !rhythm->value && rhythm->valueText == QStringLiteral("default"),
          QStringLiteral("energy_rhythm 文本默认值"));

    // ---- 3. ensureDefaults 不覆盖用户值 ----
    {
        PersonalModelParam p;
        p.key = QStringLiteral("time_capacity");
        p.value = 7.5;
        p.priorSource = QStringLiteral("user_input");
        check(pmSvc.set(p, &err), QStringLiteral("用户覆盖 time_capacity=7.5"));
        pmSvc.ensureDefaults(&err);
        const auto again = pmSvc.get(QStringLiteral("time_capacity"));
        check(again && *again->value == 7.5
                  && again->priorSource == QStringLiteral("user_input"),
              QStringLiteral("ensureDefaults 不重置用户值"));
    }

    // ---- 4. set 校验 ----
    {
        PersonalModelParam bad;
        check(!pmSvc.set(bad, &err) && !err.isEmpty(), QStringLiteral("空 key 被拒绝"));
        bad.key = QStringLiteral("x_empty");
        check(!pmSvc.set(bad, &err) && !err.isEmpty(), QStringLiteral("值全空被拒绝"));
    }

    // ---- 5. 校准记录 append-only ----
    {
        CalibrationRepository cal;
        CalibrationRecord r;
        r.paramKey = QStringLiteral("time_capacity");
        r.oldValue = QStringLiteral("6.0");
        r.newValue = QStringLiteral("6.5");
        r.dataWindow = QStringLiteral("2026-09-07..2026-09-20");
        r.rule = QStringLiteral("滚动均值 14d");
        r.trigger = QStringLiteral("periodic");
        const qint64 id = cal.append(r);
        check(id > 0, QStringLiteral("校准记录追加成功"));
        const auto list = cal.getByParam(QStringLiteral("time_capacity"));
        check(list.size() == 1 && list.front().newValue == QStringLiteral("6.5"),
              QStringLiteral("校准记录读回一致"));
        CalibrationRecord badR = r;
        badR.trigger = QStringLiteral("bad");
        check(cal.append(badR) == 0, QStringLiteral("非法 trigger 被 CHECK 拒绝"));
    }

    // ---- 6. Task 新字段（FR-C-03/04）----
    // 注：使用 09-01，落在所有指标窗口（7d/14d）之外，避免污染 E 与偏差基线数据
    {
        TaskRepository repo;
        Task t;
        t.title = QStringLiteral("线性代数 第3章");
        t.dueDate = QStringLiteral("2026-09-01");
        t.plannedMinutes = 60;
        t.triggerWhen = QStringLiteral("19:00 到家后");
        t.triggerWhere = QStringLiteral("书桌");
        t.triggerHow = QStringLiteral("翻开教材做第3章习题");
        t.taskType = QStringLiteral("learning");
        const qint64 id = repo.create(t);
        check(id > 0, QStringLiteral("任务新字段创建成功"));
        const auto back = repo.getById(id);
        check(back && back->triggerWhen == t.triggerWhen && back->triggerWhere == t.triggerWhere
                  && back->triggerHow == t.triggerHow && back->taskType == QStringLiteral("learning"),
              QStringLiteral("任务新字段读回一致"));
        t.id = id;
        t.taskType = QStringLiteral("routine");
        check(repo.update(t), QStringLiteral("任务新字段更新"));
        check(repo.getById(id)->taskType == QStringLiteral("routine"),
              QStringLiteral("task_type 更新生效"));
        t.title = QStringLiteral("非法类型任务");
        t.taskType = QStringLiteral("xxx");
        check(repo.create(t) == 0, QStringLiteral("非法 task_type 被 CHECK 拒绝"));
    }

    // ---- 7. Proposal basis_types（FR-H-02/03）----
    {
        ProposalRepository repo;
        Proposal p;
        p.targetType = QStringLiteral("plan");
        p.proposedValue = QStringLiteral("降低任务量");
        p.reason = QStringLiteral("测试");
        p.basisTypes = QStringLiteral("personal_data,research_evidence");
        const qint64 id = repo.create(p);
        check(id > 0, QStringLiteral("Proposal 带 basis_types 创建成功"));
        const auto back = repo.getById(id);
        check(back && back->basisTypes == p.basisTypes, QStringLiteral("basis_types 读回一致"));
    }

    // ---- 8. 执行率 E（design.md 1.10.2）----
    // 2026-09-20：3 任务 planned 60/60/60，actual 60/30/0（skipped）→ E = 90/180 = 0.5
    makeTask(QStringLiteral("2026-09-20"), 60, 60, QStringLiteral("completed"));
    makeTask(QStringLiteral("2026-09-20"), 60, 30, QStringLiteral("completed"));
    makeTask(QStringLiteral("2026-09-20"), 60, 0, QStringLiteral("skipped"));
    {
        const Metric e = ms.executionRate(QStringLiteral("2026-09-20"), &err);
        check(err.isEmpty() && std::abs(e.value - 0.5) < 1e-9,
              QStringLiteral("E = 0.5（90/180）"));
    }

    // ---- 9. E 无计划任务 ----
    {
        const Metric e = ms.executionRate(QStringLiteral("2026-09-12"), &err);
        check(e.value == 0.0 && e.note == QStringLiteral("无计划任务"),
              QStringLiteral("无计划任务 → 0.0 + note"));
    }

    // ---- 10. 监控频率（窗口 7 天：2 天事件 + 1 天状态 → 3/7）----
    {
        Event ev;
        ev.type = QStringLiteral("custom");
        ev.title = QStringLiteral("记录");
        ev.date = QStringLiteral("2026-09-14");
        EventRepository().append(ev);
        ev.date = QStringLiteral("2026-09-15");
        EventRepository().append(ev);
        StateSnapshot s;
        s.date = QStringLiteral("2026-09-16");
        s.energy = 3;
        StateRepository().upsert(s);
        const Metric f = ms.monitoringFrequency(QStringLiteral("2026-09-20"), 7, &err);
        check(err.isEmpty() && std::abs(f.value - 3.0 / 7.0) < 1e-9,
              QStringLiteral("监控频率 = 3/7"));
    }

    // ---- 11. 反刍信号：历史基线不足 → 不判定 ----
    // 09-15 一条复盘；09-16 E=0.3 且当日有复盘，但历史仅 1 条 → nullopt
    {
        ReviewRepository().upsert(makeReview(QStringLiteral("2026-09-15"),
                                             QStringLiteral("今天整体正常，按计划完成。")));
        makeTask(QStringLiteral("2026-09-16"), 100, 30, QStringLiteral("completed"));
        ReviewRepository().upsert(makeReview(QStringLiteral("2026-09-16"),
                                             QStringLiteral("复盘文本")));
        const auto rum = ms.rumination(QStringLiteral("2026-09-16"), &err);
        check(err.isEmpty() && !rum.has_value(), QStringLiteral("历史复盘 <3 条 → 不判定"));
    }

    // ---- 12. 反刍信号：低执行 + 超基线复盘 → 1.0 ----
    // 补齐 09-17/09-18 历史（短复盘）；09-21 E=0.3 + 长复盘 → 1.0
    {
        ReviewRepository().upsert(makeReview(QStringLiteral("2026-09-17"),
                                             QStringLiteral("今天整体正常，按计划完成。")));
        ReviewRepository().upsert(makeReview(QStringLiteral("2026-09-18"),
                                             QStringLiteral("今天整体正常，按计划完成。")));
        makeTask(QStringLiteral("2026-09-21"), 100, 30, QStringLiteral("completed"));
        Review r = makeReview(QStringLiteral("2026-09-21"),
                              QStringLiteral("为什么又没学完？我是不是真的不行？今天本来打算"
                                            "学两个小时，结果一直在刷手机，越想越焦虑，复盘也写"
                                            "不出来，感觉自己完全失控了，这周已经是第三次这样，"
                                            "我到底该怎么调整，是不是目标定得太高了，还是我这个人"
                                            "就是没有执行力……"));
        r.problems = QStringLiteral("连续低执行，焦虑，反复纠结");
        r.causes = QStringLiteral("不确定是容量不足还是动机问题");
        ReviewRepository().upsert(r);
        const auto rum = ms.rumination(QStringLiteral("2026-09-21"), &err);
        check(err.isEmpty() && rum && rum->value == 1.0,
              QStringLiteral("低执行 + 超基线复盘 → 反刍信号 1.0"));
    }

    // ---- 13. 反刍信号：低执行但短复盘 → 0.0 ----
    {
        ReviewRepository().upsert(makeReview(QStringLiteral("2026-09-21"),
                                             QStringLiteral("没完成。")));
        const auto rum = ms.rumination(QStringLiteral("2026-09-21"), &err);
        check(err.isEmpty() && rum && rum->value == 0.0,
              QStringLiteral("低执行 + 短复盘 → 反刍信号 0.0"));
    }

    // ---- 14. 反刍信号：E ≥ 0.5 → 0.0（无低执行前提）----
    {
        const auto rum = ms.rumination(QStringLiteral("2026-09-20"), &err);
        check(err.isEmpty() && rum && rum->value == 0.0,
              QStringLiteral("E=0.5 → 反刍信号 0.0"));
    }

    // ---- 15. 偏差基线（14 天窗口：09-16 E=0.3、09-19 E=1.0、09-20 E=0.5 → 均值 0.6）----
    {
        makeTask(QStringLiteral("2026-09-19"), 50, 50, QStringLiteral("completed"));
        const Metric b = ms.deviationBaseline(QStringLiteral("2026-09-20"), 14, &err);
        check(err.isEmpty() && std::abs(b.value - 0.6) < 1e-9,
              QStringLiteral("偏差基线 = 0.6（(0.3+1.0+0.5)/3）"));
    }

    // ---- 16. computeDaily 落库 + 幂等 ----
    {
        check(ms.computeDaily(QStringLiteral("2026-09-20"), &err) && err.isEmpty(),
              QStringLiteral("computeDaily 成功"));
        check(hasMetric(QStringLiteral("2026-09-20"), QStringLiteral("execution_rate"))
                  && hasMetric(QStringLiteral("2026-09-20"),
                               QStringLiteral("monitoring_frequency"))
                  && hasMetric(QStringLiteral("2026-09-20"),
                               QStringLiteral("deviation_baseline"))
                  && hasMetric(QStringLiteral("2026-09-20"), QStringLiteral("rumination")),
              QStringLiteral("四个指标全部落库"));
        const auto before = MetricRepository().get(QStringLiteral("2026-09-20"),
                                                   QStringLiteral("execution_rate"),
                                                   QStringLiteral("1d"));
        check(ms.computeDaily(QStringLiteral("2026-09-20"), &err) && err.isEmpty(),
              QStringLiteral("computeDaily 重复执行成功"));
        const auto after = MetricRepository().get(QStringLiteral("2026-09-20"),
                                                  QStringLiteral("execution_rate"),
                                                  QStringLiteral("1d"));
        check(before && after && before->id == after->id,
              QStringLiteral("重复计算幂等（同 id 不新增行）"));
    }

    // ---- 17. 日期校验 ----
    {
        check(!ms.computeDaily(QStringLiteral("bad-date"), &err) && !err.isEmpty(),
              QStringLiteral("非法日期被拒绝"));
    }

    // ---- 汇总 ----
    qInfo().noquote() << (g_failures == 0
                              ? QStringLiteral("===== SMOKE PASS =====")
                              : QStringLiteral("===== SMOKE FAIL (%1) =====").arg(g_failures));
    return g_failures == 0 ? 0 : 1;
}
