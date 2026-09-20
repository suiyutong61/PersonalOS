// Step 6 冒烟测试（控制台程序，README 3.7）
// 覆盖：StateRepository / StateService
// 使用独立测试库（PERSONOS_DB_PATH 指向临时文件），不污染真实数据。
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>

#include "TestLogging.h"
#include "database/DatabaseManager.h"
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
    const QString dbPath = QDir::temp().filePath(QStringLiteral("personos_smoke_state.db"));
    QFile::remove(dbPath);
    QFile::remove(dbPath + QStringLiteral("-wal"));
    QFile::remove(dbPath + QStringLiteral("-shm"));
    qputenv("PERSONOS_DB_PATH", dbPath.toUtf8());

    if (!DatabaseManager::instance().open()) {
        qInfo().noquote() << QStringLiteral("[FAIL] 测试库打开失败: %1")
                                 .arg(DatabaseManager::instance().lastError());
        return 1;
    }

    StateService svc;
    QString err;
    const QString date1 = QStringLiteral("2026-09-20");
    const QString date2 = QStringLiteral("2026-09-19");

    // ---- 1. 初始无记录 ----
    check(!svc.getByDate(date1).has_value(), QStringLiteral("初始无记录"));

    // ---- 2. 记录完整快照并读回 ----
    StateSnapshot s1;
    s1.date = date1;
    s1.sleepHours = 7.5;
    s1.energy = 3;
    s1.focus = 4;
    s1.mood = 2;
    s1.note = QStringLiteral("状态一般");
    check(svc.record(s1, &err) && err.isEmpty(), QStringLiteral("记录完整快照"));

    const auto back1 = svc.getByDate(date1);
    check(back1 && *back1->sleepHours == 7.5 && *back1->energy == 3 && *back1->focus == 4
              && *back1->mood == 2 && back1->note == s1.note,
          QStringLiteral("读回值一致"));

    // ---- 3. 同日期 upsert：只更新不新增 ----
    StateSnapshot s2 = s1;
    s2.sleepHours = 8.0;
    s2.energy = 4;
    check(svc.record(s2, &err) && err.isEmpty(), QStringLiteral("同日期 upsert"));
    const auto back2 = svc.getByDate(date1);
    check(back2 && back2->id == back1->id && *back2->sleepHours == 8.0 && *back2->energy == 4,
          QStringLiteral("upsert 未新增记录且值已更新"));

    // ---- 4. 校验：精力 6 被拒绝 ----
    StateSnapshot bad1 = s1;
    bad1.energy = 6;
    check(!svc.record(bad1, &err) && !err.isEmpty(), QStringLiteral("精力=6 被拒绝"));

    // ---- 5. 校验：睡眠 25 被拒绝 ----
    StateSnapshot bad2 = s1;
    bad2.sleepHours = 25.0;
    check(!svc.record(bad2, &err) && err.contains(QStringLiteral("睡眠")),
          QStringLiteral("睡眠=25 被拒绝"));

    // ---- 6. 校验：日期为空被拒绝 ----
    StateSnapshot bad3 = s1;
    bad3.date.clear();
    check(!svc.record(bad3, &err) && !err.isEmpty(), QStringLiteral("日期为空被拒绝"));

    // ---- 7. 只填 mood 的快照：其余为"未填写" ----
    StateSnapshot partial;
    partial.date = date2;
    partial.mood = 5;
    check(svc.record(partial, &err), QStringLiteral("记录部分快照（仅 mood）"));
    const auto back3 = svc.getByDate(date2);
    check(back3 && !back3->sleepHours && !back3->energy && !back3->focus && *back3->mood == 5,
          QStringLiteral("未填字段保持为空"));

    // ---- 8. 两个日期互不影响 ----
    const auto back1Final = svc.getByDate(date1);
    check(back1Final && *back1Final->sleepHours == 8.0, QStringLiteral("两个日期互不影响"));

    // ---- 汇总 ----
    qInfo().noquote() << (g_failures == 0
                              ? QStringLiteral("===== SMOKE PASS =====")
                              : QStringLiteral("===== SMOKE FAIL (%1) =====").arg(g_failures));
    return g_failures == 0 ? 0 : 1;
}
