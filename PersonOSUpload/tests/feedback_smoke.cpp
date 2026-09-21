// Step 9 冒烟测试（控制台程序，README 3.10）
// 覆盖：FeedbackService / EventRepository(correct 留痕) / ChangeLogRepository
// 使用独立测试库（PERSONOS_DB_PATH 指向临时文件），不污染真实数据。
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>

#include "TestLogging.h"
#include "database/ChangeLogRepository.h"
#include "database/DatabaseManager.h"
#include "feedback/FeedbackService.h"

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
    const QString dbPath = QDir::temp().filePath(QStringLiteral("personos_smoke_feedback.db"));
    QFile::remove(dbPath);
    QFile::remove(dbPath + QStringLiteral("-wal"));
    QFile::remove(dbPath + QStringLiteral("-shm"));
    qputenv("PERSONOS_DB_PATH", dbPath.toUtf8());

    if (!DatabaseManager::instance().open()) {
        qInfo().noquote() << QStringLiteral("[FAIL] 测试库打开失败: %1")
                                 .arg(DatabaseManager::instance().lastError());
        return 1;
    }

    FeedbackService svc;
    QString err;
    const QString day1 = QStringLiteral("2026-09-20");
    const QString day2 = QStringLiteral("2026-09-19");

    // ---- 1. 记录事件 ----
    Event e1;
    e1.date = day1;
    e1.type = QStringLiteral("task_completed");
    e1.title = QStringLiteral("完成任务A");
    const qint64 id1 = svc.recordEvent(e1, &err);
    check(id1 > 0 && err.isEmpty(), QStringLiteral("记录事件"));

    Event e2;
    e2.date = day1;
    e2.type = QStringLiteral("task_skipped");
    e2.title = QStringLiteral("跳过任务B");
    e2.description = QStringLiteral("临时有事");
    const qint64 id2 = svc.recordEvent(e2, &err);
    check(id2 > 0, QStringLiteral("记录第二条事件"));

    Event e3;
    e3.date = day2;
    e3.type = QStringLiteral("custom");
    e3.title = QStringLiteral("昨天的事件");
    check(svc.recordEvent(e3, &err) > 0, QStringLiteral("另一日期的事件"));

    // ---- 2. 校验：空日期/空类型/空标题 ----
    Event bad = e1;
    bad.date.clear();
    check(svc.recordEvent(bad, &err) == 0 && err.contains(QStringLiteral("日期")),
          QStringLiteral("空日期被拒绝"));
    bad = e1;
    bad.type.clear();
    check(svc.recordEvent(bad, &err) == 0 && err.contains(QStringLiteral("类型")),
          QStringLiteral("空类型被拒绝"));
    bad = e1;
    bad.title = QStringLiteral(" ");
    check(svc.recordEvent(bad, &err) == 0 && err.contains(QStringLiteral("标题")),
          QStringLiteral("空标题被拒绝"));

    // ---- 3. 按日查询与范围查询 ----
    check(svc.eventsOfDay(day1).size() == 2, QStringLiteral("day1 共 2 条事件"));
    check(svc.eventsOfRange(day2, day1).size() == 3, QStringLiteral("范围查询含端点共 3 条"));

    // ---- 4. 按类型聚合 ----
    const auto counts = svc.eventCountsByType(day1);
    check(counts.value(QStringLiteral("task_completed")) == 1
              && counts.value(QStringLiteral("task_skipped")) == 1,
          QStringLiteral("按类型聚合正确"));

    // ---- 5. 修正留痕（2.4.14 原则1）----
    Event fix;
    fix.type = QStringLiteral("task_completed");
    fix.title = QStringLiteral("完成任务A（修正后标题）");
    fix.description = QStringLiteral("修正后的描述");
    check(svc.correctEvent(id1, fix, QStringLiteral("原标题有误"), &err) && err.isEmpty(),
          QStringLiteral("correctEvent 成功"));

    // ---- 6. 修正后的值已生效 ----
    EventRepository eventRepo;
    const auto fixedEvent = eventRepo.getById(id1);
    check(fixedEvent && fixedEvent->title == fix.title && fixedEvent->description == fix.description,
          QStringLiteral("修正后的值已生效"));

    // ---- 7. 留痕记录：原始 → 修正 → 原因 ----
    ChangeLogRepository logRepo;
    const auto logs = logRepo.getByTarget(QStringLiteral("event"), id1);
    check(logs.size() == 1, QStringLiteral("留痕记录 1 条"));
    if (!logs.empty()) {
        const ChangeLog &log = logs.front();
        check(log.reason == QStringLiteral("原标题有误"), QStringLiteral("留痕含原因"));
        check(log.beforeSummary.contains(QStringLiteral("完成任务A"))
                  && log.afterSummary.contains(QStringLiteral("修正后标题")),
              QStringLiteral("留痕含修改前后内容"));
    }

    // ---- 8. 修正必须给原因 ----
    check(!svc.correctEvent(id2, fix, QStringLiteral(" "), &err) && !err.isEmpty(),
          QStringLiteral("无原因的修正被拒绝"));

    // ---- 9. 修正不存在的事件 ----
    check(!svc.correctEvent(999999, fix, QStringLiteral("原因"), &err) && err.contains(QStringLiteral("不存在")),
          QStringLiteral("修正不存在事件被拒绝"));

    // ---- 汇总 ----
    qInfo().noquote() << (g_failures == 0
                              ? QStringLiteral("===== SMOKE PASS =====")
                              : QStringLiteral("===== SMOKE FAIL (%1) =====").arg(g_failures));
    return g_failures == 0 ? 0 : 1;
}
