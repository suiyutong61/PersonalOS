#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include "application/ports/OperationPorts.h"
#include "application/usecases/mel/MelUseCases.h"
#include "application/usecases/migration/LegacyMigration.h"
#include "application/usecases/operations/OperationUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/embedding/DomainClassifier.h"
#include "infrastructure/embedding/LocalEmbeddingProvider.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/EmbeddingBackfill.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/knowledge/ReviewQuestionnaireSeed.h"
#include "infrastructure/knowledge/StateDefinitionsSeed.h"
#include "infrastructure/operations/SqlOperationsRepository.h"
#include "infrastructure/persistence/AuditDatabaseSink.h"
#include "infrastructure/persistence/DatabaseConnectionFactory.h"
#include "presentation/viewmodels/AppNotifier.h"
#include "presentation/viewmodels/VmSupport.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlMelRepository.h"
#include "infrastructure/persistence/SqlReviewRepository.h"
#include "infrastructure/persistence/SqlStateRepository.h"
#include "infrastructure/persistence/UserProfileBootstrap.h"

using namespace PersonOS;

namespace {

// 开发者工具：appPersonOS --db-check（README 3.4）
// 无界面验证数据库。GUI 子系统程序没有控制台输出，结果写入当前目录的
// dbcheck.log（路径、schema 版本、数据表清单）；退出码 0=OK，1=失败。
int runDbCheck()
{
    QStringList lines;
    int exitCode = 0;

    DatabaseManager &db = DatabaseManager::instance();
    if (!db.open()) {
        lines << QStringLiteral("数据库打开失败: %1").arg(db.lastError());
        lines << QStringLiteral("RESULT: FAIL");
        exitCode = 1;
    } else {
        lines << QStringLiteral("数据库路径: %1").arg(db.databasePath());
        lines << QStringLiteral("schema 版本: %1").arg(db.schemaVersion());
        lines << QStringLiteral("数据表:");

        QSqlQuery q(db.database());
        if (q.exec(QStringLiteral(
                "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"))) {
            while (q.next())
                lines << QStringLiteral("  - %1").arg(q.value(0).toString());
        } else {
            lines << QStringLiteral("  (查询失败: %1)").arg(q.lastError().text());
            exitCode = 1;
        }
        lines << QStringLiteral("RESULT: %1").arg(exitCode == 0 ? QStringLiteral("OK")
                                                                : QStringLiteral("FAIL"));
    }

    QFile report(QStringLiteral("dbcheck.log"));
    if (report.open(QIODevice::WriteOnly | QIODevice::Text))
        report.write(lines.join(u'\n').toUtf8());

    return exitCode;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // 应用标识：决定 QStandardPaths::AppLocalDataLocation 数据目录
    app.setOrganizationName(QStringLiteral("PersonalOS"));
    app.setApplicationName(QStringLiteral("PersonalOS"));

    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--db-check")))
        return runDbCheck();

    if (!DatabaseManager::instance().open()) {
        qCritical().noquote() << "数据库打开失败:" << DatabaseManager::instance().lastError();
        return 1;
    }
    // 审计落库(DR-026):线程感知 sink(每次 append 取当前线程连接,
    // 用例在后台 QtConcurrent 线程的审计写入同样落库)。static 存储期
    // 保证晚于所有后台任务析构。
    static Infrastructure::AuditDatabaseSink auditSink(
        DatabaseManager::instance().databasePath());
    Application::Audit::setSink(&auditSink);
    // 领域分类器的向量持久化落点（embedding_records_v6）；未配置则回退纯内存
    Infrastructure::DomainClassifier::configureDatabasePath(
        DatabaseManager::instance().databasePath());
    // 首次启动引导（档案 + 三种子，各自幂等；纯本地，无外发）。
    // 失败不阻断启动（依赖档案的功能在使用时给出明确错误）
    if (!Infrastructure::ensureStartupSeeded(DatabaseManager::instance().database()))
        qWarning("首次启动引导未全部完成（启动）");
    // v2 旧库升级：检测到可迁移的旧数据才执行（幂等可续跑，
    // legacy_id_maps_v6 保证）。注意旧表由迁移 v1 创建、每个库都存在,
    // 不能以"表存在"为条件;active 旧计划按设计不迁移("不冒充 MEL")。
    {
        QSqlQuery legacyCheck(DatabaseManager::instance().database());
        const bool hasLegacyData =
            legacyCheck.exec(QStringLiteral(
                "SELECT (SELECT COUNT(*) FROM goals) + (SELECT COUNT(*) FROM tasks) "
                "+ (SELECT COUNT(*) FROM events) + (SELECT COUNT(*) FROM reviews) "
                "+ (SELECT COUNT(*) FROM state_snapshots) "
                "+ (SELECT COUNT(*) FROM plans WHERE status<>'active')"))
            && legacyCheck.next() && legacyCheck.value(0).toInt() > 0;
        if (hasLegacyData) {
            Infrastructure::QtSystemClock clock;
            Infrastructure::QtUidGenerator uids;
            const auto database = DatabaseManager::instance().database();
            Infrastructure::SqlGoalRepository goals(database, clock);
            Infrastructure::SqlMelRepository mels(database, clock);
            Infrastructure::SqlReviewRepository reviews(database, clock);
            Infrastructure::SqlStateRepository states(database, clock);
            Application::LegacyMigration migration(database, goals, mels, reviews, states,
                                                  uids, clock);
            if (!migration.run())
                qWarning("旧库迁移失败（启动）");
        }
    }
    // 逾期 MEL 结算、默认 Deadline 提醒规则与提醒补发
    // （StartupRecovery，E2E-05；重复启动幂等）。
    // 桌面端尚无通知投递通道：投递失败如实记录（不伪装成功）。
    {
        Infrastructure::QtSystemClock clock;
        Infrastructure::QtUidGenerator uids;
        const auto database = DatabaseManager::instance().database();
        Infrastructure::SqlMelRepository melRepo(database, clock);
        Application::MelUseCases melUseCases(melRepo, uids, clock);
        Infrastructure::SqlOperationsRepository opsRepo(database, clock);
        // 应用内横幅为 v1 唯一通知通道（用户决策 2026-09-30）
        Application::ReminderService reminders(opsRepo, melRepo,
                                               Presentation::AppNotifier::instance(),
                                               uids, clock);
        const auto startupUserId = Presentation::activeUserUid(database);
        if (startupUserId) {
            Application::StartupRecovery recovery(melRepo, melUseCases, reminders,
                                                  *startupUserId, clock);
            recovery.run();
        }
    }

    // 九页 UI 全部由页面级 ViewModel（QML_ELEMENT）驱动；
    // 旧 ApplicationService 已退役，不再作为 QML 上下文入口（DD-001 §11）。
    QQmlApplicationEngine engine;

    // 开发者工具：appPersonOS --ui-check
    // 验证 QML 加载与类型注册：加载成功后 1.5 秒自动退出（0=OK，1=QML 失败）
    const bool uiCheck = args.contains(QStringLiteral("--ui-check"));
    if (uiCheck) {
        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreated, &app,
            [&app](QObject *obj, const QUrl &) {
                if (!obj) {
                    QCoreApplication::exit(1);
                    return;
                }
                QTimer::singleShot(1500, &app, []() { QCoreApplication::exit(0); });
            },
            Qt::QueuedConnection);
    }

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    // 应用内提醒横幅单例：QML 与通知端口共用同一实例。独立 URI
    // PersonOS.App 注册——与 QML 模块 URI（PersonOS）区分，避免模块
    // 插件被同名手动注册挡住（smoke 目标曾因此类型全不可用）。
    qmlRegisterSingletonInstance("PersonOS.App", 1, 0, "AppNotifier",
                                 &Presentation::AppNotifier::instance());
    engine.loadFromModule("PersonOS", "Main");

    if (uiCheck)
        return app.exec();

    // 窗口显示后（不阻塞启动）：后台预热本地嵌入模型（130MB 加载放后台，
    // 消除首次导入/领域推荐的 1-2 秒等待），随后增量回填存量条目的向量行
    // （每批限量，缺行即断点续作；模型不可用时静默跳过，DR-013）
    QTimer::singleShot(300, &app, []() {
        QtConcurrent::run([]() {
            Infrastructure::LocalEmbeddingProvider &provider =
                Infrastructure::sharedLocalEmbedding();
            if (!provider.isReady())
                return;
            Infrastructure::DatabaseConnectionFactory factory(
                DatabaseManager::instance().databasePath());
            QString openError;
            QSqlDatabase workerDb = factory.openForCurrentThread(
                QStringLiteral("embedding_backfill"), &openError);
            if (!workerDb.isOpen())
                return;
            {
                Infrastructure::EmbeddingBackfill backfill(workerDb, provider);
                backfill.runOneBatch(20);
            }
            Infrastructure::DatabaseConnectionFactory::closeCurrentThreadConnection(
                QStringLiteral("embedding_backfill"));
        });
    });

    // 提醒周期扫描（主线程，30 秒；R3.3.1 程序运行期间可靠触发）：
    // 确保活跃 MEL 默认 Deadline 规则 → 调度到期 → 投递应用内横幅。
    // 每 tick 幂等（幂等键/attempt_count），失败只记录不改变业务状态。
    // 启动即先扫一次（补发错过提醒），随后周期执行；栈对象随 main 作用域
    // 存活至事件循环结束。
    {
        const auto scanReminders = []() {
            // 整体提醒开关（设置页）：关闭时保持完全静默，规则与投递记录保留
            if (!Presentation::remindersGloballyEnabled())
                return;
            const auto database = DatabaseManager::instance().database();
            Infrastructure::QtSystemClock clock;
            Infrastructure::QtUidGenerator uids;
            Infrastructure::SqlMelRepository melRepo(database, clock);
            Infrastructure::SqlOperationsRepository opsRepo(database, clock);
            Application::ReminderService reminders(opsRepo, melRepo,
                                                   Presentation::AppNotifier::instance(),
                                                   uids, clock);
            const auto userId = Presentation::activeUserUid(database);
            if (!userId)
                return;
            reminders.ensureMelDeadlineReminders(*userId, 100);
            reminders.scheduleDue(clock.utcIso());
            reminders.dispatchPending();
        };
        scanReminders();
        QTimer reminderTimer;
        QObject::connect(&reminderTimer, &QTimer::timeout, &app, scanReminders);
        reminderTimer.start(30000);
        return QGuiApplication::exec();
    }
}
