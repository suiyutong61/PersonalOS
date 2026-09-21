#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTimer>

#include "database/DatabaseManager.h"
#include "services/ApplicationService.h"

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

    // ApplicationService：QML 唯一入口门面（README 3.3.1）
    PersonOS::ApplicationService service;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appService"), &service);

    // 开发者工具：appPersonOS --ui-check
    // 验证 QML 加载与上下文注入：加载成功后 1.5 秒自动退出（0=OK，1=QML 失败）
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
    engine.loadFromModule("PersonOS", "Main");

    if (uiCheck)
        return app.exec();

    return QGuiApplication::exec();
}
