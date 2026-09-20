// Step 11 冒烟测试（控制台程序，README 3.12）
// 覆盖：AiConfig 加载 / OpenAiCompatibleAdapter（真实 DeepSeek 调用）/
//       AiEngine::analyzeDailyReview / proposeChange
// 前置条件：%LOCALAPPDATA%/PersonalOS/PersonalOS/ai_config.json 已配置（不入库）。
#include <functional>

#include <QCoreApplication>
#include <QDate>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTimer>

#include "TestLogging.h"
#include "ai/AiConfig.h"
#include "ai/AiEngine.h"
#include "ai/OpenAiCompatibleAdapter.h"
#include "database/DatabaseManager.h"
#include "execution/ExecutionService.h"
#include "planning/PlanningService.h"
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

// 阻塞等待异步回调（最多 timeoutMs），返回是否超时
bool waitFor(const std::function<void(const std::function<void()> &)> &run, int timeoutMs)
{
    QEventLoop loop;
    bool finished = false;
    bool timedOut = false;
    run([&]() {
        finished = true;
        loop.quit();
    });
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        timedOut = true;
        loop.quit();
    });
    timer.start(timeoutMs);
    loop.exec();
    return finished && !timedOut;
}

} // namespace

int main(int argc, char *argv[])
{
    Test::installMessageHandler();

    QCoreApplication app(argc, argv);

    // 应用标识：决定 QStandardPaths::AppLocalDataLocation（AiConfig 配置文件位置）
    QCoreApplication::setOrganizationName(QStringLiteral("PersonalOS"));
    QCoreApplication::setApplicationName(QStringLiteral("PersonalOS"));

    // 独立测试库
    const QString dbPath = QDir::temp().filePath(QStringLiteral("personos_smoke_ai.db"));
    QFile::remove(dbPath);
    QFile::remove(dbPath + QStringLiteral("-wal"));
    QFile::remove(dbPath + QStringLiteral("-shm"));
    qputenv("PERSONOS_DB_PATH", dbPath.toUtf8());

    if (!DatabaseManager::instance().open()) {
        qInfo().noquote() << QStringLiteral("[FAIL] 测试库打开失败: %1")
                                 .arg(DatabaseManager::instance().lastError());
        return 1;
    }

    // ---- 0. AI 配置加载（不打印 key）----
    const AiConfig config = AiConfig::load();
    check(config.hasKey(), QStringLiteral("已加载 AI 配置（含 key）"));
    if (!config.hasKey()) {
        qInfo().noquote() << QStringLiteral("[FAIL] 请在 %LOCALAPPDATA%/PersonalOS/PersonalOS/ 配置 ai_config.json");
        return 1;
    }

    // ---- 1. 准备当日事实数据 ----
    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    PlanningService planning;
    ExecutionService execution;
    StateService states;
    QString err;

    Task t1;
    t1.title = QStringLiteral("学习高数");
    t1.plannedMinutes = 120;
    const qint64 task1 = planning.addTaskToPlan(today, t1, &err);
    Task t2;
    t2.title = QStringLiteral("背单词");
    t2.plannedMinutes = 30;
    const qint64 task2 = planning.addTaskToPlan(today, t2, &err);
    execution.startTask(task1, &err);
    execution.completeTask(task1, 90, &err);
    execution.skipTask(task2, QStringLiteral("临时有事"), &err);

    StateSnapshot s;
    s.date = today;
    s.sleepHours = 6.5;
    s.energy = 3;
    s.focus = 2;
    check(task1 > 0 && task2 > 0 && states.record(s, &err), QStringLiteral("准备：当日事实数据"));

    // ---- 2. analyzeDailyReview（真实 API 调用）----
    AiEngine engine;
    engine.setConfig(config);
    bool chatOk1 = false;
    Review review;
    QString err1;
    const bool done1 = waitFor(
        [&](const std::function<void()> &done) {
            engine.analyzeDailyReview(today, [&](bool ok, const Review &r, const QString &e) {
                chatOk1 = ok;
                review = r;
                err1 = e;
                done();
            });
        },
        180000);
    check(done1 && chatOk1 && err1.isEmpty(), QStringLiteral("AI 复盘分析调用成功（%1）").arg(err1));
    check(!review.summary.trimmed().isEmpty(), QStringLiteral("AI 输出当日总结"));
    check(!review.problems.trimmed().isEmpty(), QStringLiteral("AI 输出问题分析"));
    if (chatOk1) {
        qInfo().noquote() << QStringLiteral("  AI 摘要: %1").arg(
            review.summary.left(60).replace(u'\n', u' '));
    }

    // ---- 3. proposeChange（AI 生成变更提案，绝不自动生效）----
    bool chatOk2 = false;
    Proposal proposal;
    QString err2;
    const bool done2 = waitFor(
        [&](const std::function<void()> &done) {
            engine.proposeChange(QStringLiteral("近期学习任务完成率偏低（约 50%），希望提高计划完成率"),
                                 [&](bool ok, const Proposal &p, const QString &e) {
                                     chatOk2 = ok;
                                     proposal = p;
                                     err2 = e;
                                     done();
                                 });
        },
        180000);
    check(done2 && chatOk2 && err2.isEmpty(), QStringLiteral("AI 生成变更提案成功（%1）").arg(err2));
    check(proposal.status == QStringLiteral("draft"), QStringLiteral("提案状态为 draft（不自动生效）"));
    check(!proposal.proposedValue.trimmed().isEmpty() && !proposal.reason.trimmed().isEmpty(),
          QStringLiteral("提案含建议与理由"));
    if (chatOk2) {
        qInfo().noquote() << QStringLiteral("  提案: %1 → %2").arg(proposal.currentValue.left(30),
                                                                   proposal.proposedValue.left(30));
    }

    // ---- 4. 错误 key 路径（适配器错误处理）----
    OpenAiCompatibleAdapter badAdapter(config.baseUrl, QStringLiteral("sk-invalid-key-for-test"),
                                       config.model);
    bool chatOk3 = true;
    QString err3;
    const bool done3 = waitFor(
        [&](const std::function<void()> &done) {
            badAdapter.chat(QStringLiteral("hi"), QStringLiteral("hi"),
                            [&](bool ok, const QString &, const QString &e) {
                                chatOk3 = ok;
                                err3 = e;
                                done();
                            });
        },
        60000);
    check(done3 && !chatOk3 && !err3.isEmpty(), QStringLiteral("无效 key 返回错误（%1）").arg(err3.left(50)));

    // ---- 汇总 ----
    qInfo().noquote() << (g_failures == 0
                              ? QStringLiteral("===== SMOKE PASS =====")
                              : QStringLiteral("===== SMOKE FAIL (%1) =====").arg(g_failures));
    return g_failures == 0 ? 0 : 1;
}
