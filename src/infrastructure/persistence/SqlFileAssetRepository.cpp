#include "infrastructure/persistence/SqlFileAssetRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

namespace {

std::optional<Domain::FileAsset> assetFromQuery(QSqlQuery &query)
{
    const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
    if (!uid)
        return std::nullopt;
    Domain::FileAsset asset;
    asset.uid = *uid;
    asset.relativePath = query.value("relative_path").toString().toStdString();
    asset.mimeType = query.value("mime_type").toString().toStdString();
    asset.byteSize = query.value("byte_size").toLongLong();
    asset.sha256 = query.value("sha256").toString().toStdString();
    asset.originalName = query.value("original_name").toString().toStdString();
    if (const auto state = Domain::fileStorageStateFrom(
            query.value("storage_state").toString().toStdString()))
        asset.storageState = *state;
    asset.importedAt = query.value("imported_at").toString().toStdString();
    asset.revision = query.value("revision").toInt();
    return asset;
}

} // namespace

SqlFileAssetRepository::SqlFileAssetRepository(QSqlDatabase database,
                                               const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

std::optional<Domain::FileAsset> SqlFileAssetRepository::findBySha256(const std::string &sha256)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, source_id, relative_path, mime_type, byte_size, sha256, "
        "original_name, storage_state, imported_at, revision "
        "FROM file_assets_v5 WHERE sha256=?"));
    query.addBindValue(QString::fromStdString(sha256));
    if (!query.exec() || !query.next())
        return std::nullopt;
    return assetFromQuery(query);
}

Application::Result<Domain::FileAsset, Application::ApplicationError>
SqlFileAssetRepository::registerAsset(const Domain::FileAsset &asset)
{
    if (!asset.isValid())
        return Application::Result<Domain::FileAsset, Application::ApplicationError>::failure(
            {Application::ErrorCode::Validation, "file asset invalid", {}, false});

    // 重复检测：同指纹已存在 → 返回已有记录（幂等注册）
    if (const auto existing = findBySha256(asset.sha256))
        return Application::Result<Domain::FileAsset, Application::ApplicationError>::success(
            *existing);

    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO file_assets_v5(uid, source_id, relative_path, mime_type, byte_size, "
        "sha256, original_name, storage_state, imported_at, created_at, updated_at, revision) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(asset.uid.value()));
    query.addBindValue(QVariant());   // source_id 由导入层建立关联（可空）
    query.addBindValue(QString::fromStdString(asset.relativePath));
    query.addBindValue(QString::fromStdString(asset.mimeType));
    query.addBindValue(asset.byteSize);
    query.addBindValue(QString::fromStdString(asset.sha256));
    query.addBindValue(QString::fromStdString(asset.originalName));
    query.addBindValue(QString::fromStdString(Domain::toString(asset.storageState)));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return Application::Result<Domain::FileAsset, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "file asset register failed",
             query.lastError().text().toStdString(), false});

    Domain::FileAsset saved = asset;
    saved.importedAt = now;
    return Application::Result<Domain::FileAsset, Application::ApplicationError>::success(
        std::move(saved));
}

Application::Result<Domain::FileAsset, Application::ApplicationError>
SqlFileAssetRepository::promote(const Domain::Uid &assetUid, int expectedRevision)
{
    QSqlQuery current(m_database);
    current.prepare(QStringLiteral(
        "SELECT uid, source_id, relative_path, mime_type, byte_size, sha256, "
        "original_name, storage_state, imported_at, revision FROM file_assets_v5 WHERE uid=?"));
    current.addBindValue(QString::fromStdString(assetUid.value()));
    if (!current.exec() || !current.next())
        return Application::Result<Domain::FileAsset, Application::ApplicationError>::failure(
            {Application::ErrorCode::NotFound, "file asset not found", {}, false});
    const auto before = assetFromQuery(current);
    if (!before)
        return Application::Result<Domain::FileAsset, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "file asset parse failed", {}, false});
    if (before->storageState != Domain::FileStorageState::Quarantine)
        return Application::Result<Domain::FileAsset, Application::ApplicationError>::failure(
            {Application::ErrorCode::Conflict, "only quarantine assets can be promoted", {},
             false});

    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery update(m_database);
    update.prepare(QStringLiteral(
        "UPDATE file_assets_v5 SET storage_state='managed', updated_at=?, "
        "revision=revision+1 WHERE uid=? AND revision=?"));
    update.addBindValue(QString::fromStdString(now));
    update.addBindValue(QString::fromStdString(assetUid.value()));
    update.addBindValue(expectedRevision);
    if (!update.exec())
        return Application::Result<Domain::FileAsset, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "file asset promote failed",
             update.lastError().text().toStdString(), false});
    if (update.numRowsAffected() == 0)
        return Application::Result<Domain::FileAsset, Application::ApplicationError>::failure(
            {Application::ErrorCode::Conflict, "file asset revision conflict", {}, false});

    Domain::FileAsset promoted = *before;
    promoted.storageState = Domain::FileStorageState::Managed;
    promoted.revision = expectedRevision + 1;
    return Application::Result<Domain::FileAsset, Application::ApplicationError>::success(
        std::move(promoted));
}

} // namespace PersonOS::Infrastructure
