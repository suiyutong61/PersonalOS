// Step 12 冒烟测试（控制台程序，README 3.13）
// 覆盖：ProposalRepository / VersionRepository / EvolutionService
//       （propose → decide → applyApproved → ChangeLog + Version 全链路）
// 使用独立测试库（PERSONOS_DB_PATH 指向临时文件），不污染真实数据。
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>

#include "TestLogging.h"
#include "database/ChangeLogRepository.h"
#include "database/DatabaseManager.h"
#include "evolution/EvolutionService.h"

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

Proposal makeProposal()
{
    Proposal p;
    p.targetType = QStringLiteral("strategy");
    p.currentValue = QStringLiteral("每天固定 5 小时学习");
    p.proposedValue = QStringLiteral("每天 4 小时核心学习 + 弹性任务");
    p.reason = QStringLiteral("过去 21 天完成率持续低于预期");
    p.evidence = QStringLiteral("完成率均值 65%");
    p.expectedEffect = QStringLiteral("提高计划完成率");
    p.risk = QStringLiteral("可能降低理论学习总量");
    return p;
}

} // namespace

int main(int argc, char *argv[])
{
    Test::installMessageHandler();

    QCoreApplication app(argc, argv);

    // 独立测试库
    const QString dbPath = QDir::temp().filePath(QStringLiteral("personos_smoke_evolution.db"));
    QFile::remove(dbPath);
    QFile::remove(dbPath + QStringLiteral("-wal"));
    QFile::remove(dbPath + QStringLiteral("-shm"));
    qputenv("PERSONOS_DB_PATH", dbPath.toUtf8());

    if (!DatabaseManager::instance().open()) {
        qInfo().noquote() << QStringLiteral("[FAIL] 测试库打开失败: %1")
                                 .arg(DatabaseManager::instance().lastError());
        return 1;
    }

    EvolutionService svc;
    QString err;

    // ---- 1. 提案校验 ----
    Proposal bad = makeProposal();
    bad.reason.clear();
    check(svc.propose(bad, &err) == 0 && err.contains(QStringLiteral("理由")),
          QStringLiteral("缺理由的提案被拒绝"));
    bad = makeProposal();
    bad.proposedValue.clear();
    check(svc.propose(bad, &err) == 0 && err.contains(QStringLiteral("建议值")),
          QStringLiteral("缺建议值的提案被拒绝"));
    bad = makeProposal();
    bad.targetType.clear();
    check(svc.propose(bad, &err) == 0, QStringLiteral("缺对象类型的提案被拒绝"));

    // ---- 2. 提交提案 → review ----
    const qint64 id1 = svc.propose(makeProposal(), &err);
    check(id1 > 0 && err.isEmpty(), QStringLiteral("提交提案"));
    check(svc.pendingProposals().size() == 1, QStringLiteral("待审提案 1 条"));

    // ---- 3. 用户拒绝 ----
    check(svc.decide(id1, false, &err) && err.isEmpty(), QStringLiteral("用户拒绝提案"));
    check(svc.pendingProposals().empty(), QStringLiteral("拒绝后待审列表为空"));

    // ---- 4. 重复决定被拒绝 ----
    check(!svc.decide(id1, true, &err) && err.contains(QStringLiteral("review")),
          QStringLiteral("非待审提案不能再次决定"));

    // ---- 5. 新提案 → 批准 → 生效 ----
    const qint64 id2 = svc.propose(makeProposal(), &err);
    check(svc.decide(id2, true, &err), QStringLiteral("用户批准提案"));
    check(svc.applyApproved(id2, &err) && err.isEmpty(), QStringLiteral("生效已批准提案"));

    // ---- 6. 版本链：首个版本 v0.1 ----
    const auto current = svc.currentVersion();
    check(current && current->versionNumber == QStringLiteral("v0.1"), QStringLiteral("首个版本 v0.1"));
    check(current && current->parentVersion.isEmpty(), QStringLiteral("首版本无父版本"));

    // ---- 7. 留痕：ChangeLog 关联提案与版本 ----
    ChangeLogRepository logs;
    const auto entries = logs.getByTarget(QStringLiteral("strategy"), 0);
    check(entries.size() == 1, QStringLiteral("留痕 1 条"));
    if (!entries.empty()) {
        const ChangeLog &log = entries.front();
        check(log.proposalId == id2 && log.versionId == current->id, QStringLiteral("留痕关联提案+版本"));
        check(log.beforeSummary.contains(QStringLiteral("5 小时"))
                  && log.afterSummary.contains(QStringLiteral("4 小时"))
                  && log.reason.contains(QStringLiteral("完成率")),
              QStringLiteral("留痕含修改前后+理由"));
    }

    // ---- 8. 已生效提案不能再次生效 ----
    check(!svc.applyApproved(id2, &err) && err.contains(QStringLiteral("approved")),
          QStringLiteral("已生效提案不能重复生效"));

    // ---- 9. 未批准提案不能生效 ----
    const qint64 id3 = svc.propose(makeProposal(), &err);
    check(!svc.applyApproved(id3, &err) && err.contains(QStringLiteral("approved")),
          QStringLiteral("未批准提案不能生效"));

    // ---- 10. 第二个变更 → v0.2 且父版本为 v0.1 ----
    svc.decide(id3, true, &err);
    check(svc.applyApproved(id3, &err), QStringLiteral("第二个变更生效"));
    const auto v2 = svc.currentVersion();
    check(v2 && v2->versionNumber == QStringLiteral("v0.2") && v2->parentVersion == QStringLiteral("v0.1"),
          QStringLiteral("版本链 v0.1 → v0.2"));
    check(svc.versions().size() == 2, QStringLiteral("共 2 个版本"));

    // ---- 11. 不存在的提案 ----
    check(!svc.decide(999999, true, &err) && err.contains(QStringLiteral("不存在")),
          QStringLiteral("不存在提案被拒绝"));

    // ---- 汇总 ----
    qInfo().noquote() << (g_failures == 0
                              ? QStringLiteral("===== SMOKE PASS =====")
                              : QStringLiteral("===== SMOKE FAIL (%1) =====").arg(g_failures));
    return g_failures == 0 ? 0 : 1;
}
