#pragma once

#include <optional>
#include <string>

#include "domain/foundation/Uid.h"

// 领域清单仓储端口（DR-038；数据库设计 §3.2）
// 清单以版本化 JSON 入库；运行时只读取受管数据，不访问网络。
namespace PersonOS::Application {

struct ManifestVersionRecord
{
    std::string versionUid;
    int versionNo = 0;
    std::string manifestJson;
    std::string schemaVersion;
    std::string contentHash;
    std::string validFrom;
};

class DomainManifestRepository
{
public:
    virtual ~DomainManifestRepository() = default;

    // domain_code → 清单条目 uid（如 learning）
    virtual std::optional<Domain::Uid> findManifestByCode(const std::string &domainCode) = 0;

    // 最新有效版本（status=active，按 version_no 取最大）
    virtual std::optional<ManifestVersionRecord> latestActiveVersion(
        const Domain::Uid &manifestUid) = 0;

    // 指定版本（回退/追溯用）
    virtual std::optional<ManifestVersionRecord> versionByUid(
        const std::string &versionUid) = 0;
};

} // namespace PersonOS::Application
