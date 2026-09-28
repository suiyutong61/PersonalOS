#pragma once

#include <QSqlDatabase>

#include <optional>

#include "application/foundation/Result.h"
#include "application/ports/GoalRepository.h"   // SaveResult
#include "domain/foundation/Clock.h"
#include "domain/knowledge/FileAsset.h"

// 受管文件资产仓储（数据库设计 §5.1 file_assets_v5；DD-001 §8.1）
// 流程：register（quarantine，内容指纹去重）→ promote（quarantine → managed）。
namespace PersonOS::Infrastructure {

class SqlFileAssetRepository
{
public:
    explicit SqlFileAssetRepository(QSqlDatabase database, const Domain::Clock &clock);

    // 按 SHA-256 查重；已存在同指纹资产时返回已有记录（重复导入检测）
    std::optional<Domain::FileAsset> findBySha256(const std::string &sha256);

    // 登记资产（默认隔离区）；失败返回错误
    Application::Result<Domain::FileAsset, Application::ApplicationError> registerAsset(
        const Domain::FileAsset &asset);

    // 隔离区 → 正式仓库（校验通过后调用）；revision 守卫
    Application::Result<Domain::FileAsset, Application::ApplicationError> promote(
        const Domain::Uid &assetUid, int expectedRevision);

private:
    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
