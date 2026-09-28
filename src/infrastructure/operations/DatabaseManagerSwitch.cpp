#include "infrastructure/operations/DatabaseManagerSwitch.h"

#include <QDateTime>
#include <QFile>

#include "database/DatabaseManager.h"

namespace PersonOS::Infrastructure {

namespace {

bool moveFile(const QString &from, const QString &to)
{
    if (QFile::exists(to))
        QFile::remove(to);
    return QFile::rename(from, to);
}

} // namespace

QString DatabaseManagerSwitch::activeDatabasePath()
{
    return DatabaseManager::instance().databasePath();
}

Application::Result<void, Application::ApplicationError>
DatabaseManagerSwitch::switchTo(const std::string &replacementDbPath,
                                std::string *preRestoreSnapshotPath)
{
    const QString livePath = activeDatabasePath();
    const QString replacement = QString::fromStdString(replacementDbPath);
    if (!QFile::exists(replacement))
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::NotFound, "replacement database not found", {}, false});

    const QString snapshotPath =
        livePath + QStringLiteral(".pre-restore-")
        + QString::number(QDateTime::currentMSecsSinceEpoch());
    if (preRestoreSnapshotPath)
        *preRestoreSnapshotPath = snapshotPath.toStdString();

    // 1) 关闭当前连接（受控点：调用方保证无存活副本）
    DatabaseManager::instance().closeConnection();

    // 2) 原文件改名保护（恢复前安全快照；失败则重开原库并报错）
    if (!moveFile(livePath, snapshotPath)) {
        DatabaseManager::instance().open();
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "pre-restore snapshot failed", {}, false});
    }

    // 3) 换入备份文件（复制到目标路径再改名，保证目标文件完整）
    if (!QFile::copy(replacement, livePath + QStringLiteral(".incoming"))) {
        moveFile(snapshotPath, livePath);   // 回退原文件
        DatabaseManager::instance().open();
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "replace copy failed", {}, false});
    }
    if (!moveFile(livePath + QStringLiteral(".incoming"), livePath)) {
        moveFile(snapshotPath, livePath);
        DatabaseManager::instance().open();
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "replace rename failed", {}, false});
    }

    // 4) 重开并迁移；失败回退原文件
    if (!DatabaseManager::instance().open()) {
        const QString error = DatabaseManager::instance().lastError();
        moveFile(livePath, livePath + QStringLiteral(".failed"));
        moveFile(snapshotPath, livePath);
        DatabaseManager::instance().open();
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage,
             "restored database failed to open: " + error.toStdString(), {}, false});
    }

    // 成功：恢复前快照保留在 snapshotPath（安全快照不自动删除，由用户/保留策略管理）
    return Application::Result<void, Application::ApplicationError>::success();
}

} // namespace PersonOS::Infrastructure
