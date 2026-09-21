// Step 5 冒烟测试（控制台程序，README 3.6）
// 覆盖：GoalRepository / CoreValueRepository / PrincipleRepository / CoreAndGoalService
// 使用独立测试库（PERSONOS_DB_PATH 指向临时文件），不污染真实数据。
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>

#include "TestLogging.h"
#include "database/DatabaseManager.h"
#include "goals/CoreAndGoalService.h"

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
    const QString dbPath = QDir::temp().filePath(QStringLiteral("personos_smoke_goals.db"));
    QFile::remove(dbPath);
    QFile::remove(dbPath + QStringLiteral("-wal"));
    QFile::remove(dbPath + QStringLiteral("-shm"));
    qputenv("PERSONOS_DB_PATH", dbPath.toUtf8());

    if (!DatabaseManager::instance().open()) {
        qInfo().noquote() << QStringLiteral("[FAIL] 测试库打开失败: %1")
                                 .arg(DatabaseManager::instance().lastError());
        return 1;
    }

    CoreAndGoalService svc;
    QString err;

    // ---- 1. 目标链 vision → long_term → annual → monthly → weekly ----
    Goal vision;
    vision.level = QStringLiteral("vision");
    vision.title = QStringLiteral("成为终身学习者");
    const qint64 visionId = svc.createGoal(vision, &err);
    check(visionId > 0 && err.isEmpty(), QStringLiteral("创建 vision 目标"));

    Goal longTerm;
    longTerm.level = QStringLiteral("long_term");
    longTerm.title = QStringLiteral("考上 985 硕士");
    longTerm.parentId = visionId;
    const qint64 longTermId = svc.createGoal(longTerm, &err);
    check(longTermId > 0, QStringLiteral("创建 long_term 目标（挂 vision 下）"));

    Goal annual;
    annual.level = QStringLiteral("annual");
    annual.title = QStringLiteral("2027 考研上岸");
    annual.parentId = longTermId;
    const qint64 annualId = svc.createGoal(annual, &err);
    check(annualId > 0, QStringLiteral("创建 annual 目标"));

    Goal monthly;
    monthly.level = QStringLiteral("monthly");
    monthly.title = QStringLiteral("10 月完成数学一轮");
    monthly.parentId = annualId;
    const qint64 monthlyId = svc.createGoal(monthly, &err);
    check(monthlyId > 0, QStringLiteral("创建 monthly 目标"));

    Goal weekly;
    weekly.level = QStringLiteral("weekly");
    weekly.title = QStringLiteral("本周完成高数第 3 章");
    weekly.parentId = monthlyId;
    const qint64 weeklyId = svc.createGoal(weekly, &err);
    check(weeklyId > 0, QStringLiteral("创建 weekly 目标"));

    // ---- 2. 层级树读取 ----
    check(svc.goalHierarchy().size() == 5, QStringLiteral("层级树共 5 个目标"));
    check(svc.goalsByLevel(QStringLiteral("weekly")).size() == 1, QStringLiteral("按层级查询 weekly"));
    check(svc.goal(weeklyId) && svc.goal(weeklyId)->parentId == monthlyId,
          QStringLiteral("weekly 的父目标正确"));

    // ---- 3. 校验：非法层级 ----
    Goal badLevel;
    badLevel.level = QStringLiteral("forever");
    badLevel.title = QStringLiteral("非法层级");
    check(svc.createGoal(badLevel, &err) == 0 && !err.isEmpty(), QStringLiteral("非法层级被拒绝"));

    // ---- 4. 校验：空标题 ----
    Goal badTitle;
    badTitle.level = QStringLiteral("weekly");
    badTitle.title = QStringLiteral("   ");
    check(svc.createGoal(badTitle, &err) == 0, QStringLiteral("空标题被拒绝"));

    // ---- 5. 校验：父目标不存在 ----
    Goal orphan;
    orphan.level = QStringLiteral("weekly");
    orphan.title = QStringLiteral("孤儿目标");
    orphan.parentId = 999999;
    check(svc.createGoal(orphan, &err) == 0 && err.contains(QStringLiteral("父目标不存在")),
          QStringLiteral("父目标不存在被拒绝"));

    // ---- 6. 校验：环检测（把 vision 挂到 weekly 下）----
    Goal cycle = *svc.goal(visionId);
    cycle.parentId = weeklyId;
    check(!svc.updateGoal(cycle, &err) && err.contains(QStringLiteral("环")),
          QStringLiteral("环检测生效（vision → weekly 被拒）"));

    // ---- 7. 校验：自身为父 ----
    Goal self = *svc.goal(annualId);
    self.parentId = annualId;
    check(!svc.updateGoal(self, &err), QStringLiteral("自身为父被拒绝"));

    // ---- 8. 更新标题与优先级 ----
    Goal updated = *svc.goal(visionId);
    updated.title = QStringLiteral("成为终身学习者 v2");
    updated.priority = 90;
    check(svc.updateGoal(updated, &err) && svc.goal(visionId)->title == updated.title,
          QStringLiteral("更新目标标题/优先级"));
    check(svc.goal(visionId)->priority == 90, QStringLiteral("优先级已更新为 90"));

    // ---- 9. 核心价值 ----
    CoreValue v;
    v.name = QStringLiteral("成长");
    v.description = QStringLiteral("持续进步");
    const qint64 valueId = svc.createCoreValue(v, &err);
    check(valueId > 0, QStringLiteral("创建核心价值"));
    check(svc.createCoreValue(v, &err) == 0 && !err.isEmpty(), QStringLiteral("核心价值重名被拒绝"));
    check(svc.coreValues().size() == 1, QStringLiteral("核心价值共 1 条"));

    v.id = valueId;
    v.description = QStringLiteral("长期主义成长");
    check(svc.updateCoreValue(v, &err) && svc.coreValues().front().description == v.description,
          QStringLiteral("更新核心价值"));

    // ---- 10. 原则 ----
    Principle p;
    p.text = QStringLiteral("不以长期健康换取短期成绩");
    const qint64 principleId = svc.createPrinciple(p, &err);
    check(principleId > 0, QStringLiteral("创建原则"));
    check(svc.principles().size() == 1, QStringLiteral("原则共 1 条"));

    p.id = principleId;
    p.text = QStringLiteral("不以长期健康换取短期成绩（修订）");
    check(svc.updatePrinciple(p, &err) && svc.principles().front().text == p.text,
          QStringLiteral("更新原则"));

    // ---- 汇总 ----
    qInfo().noquote() << (g_failures == 0
                              ? QStringLiteral("===== SMOKE PASS =====")
                              : QStringLiteral("===== SMOKE FAIL (%1) =====").arg(g_failures));
    return g_failures == 0 ? 0 : 1;
}
