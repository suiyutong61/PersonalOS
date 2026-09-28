#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "domain/foundation/Uid.h"

// 受管文件资产（DD-001 §8.1；数据库设计 §5.1 file_assets_v5）
// 纪律：文件先进入隔离区（quarantine），校验指纹与类型后才转为 managed；
// 业务模块通过资产 UID 访问，不直接拼接内部路径。
namespace PersonOS::Domain {

enum class FileStorageState { Quarantine, Managed, Missing, Corrupt };

inline std::string toString(FileStorageState s)
{
    switch (s) {
    case FileStorageState::Quarantine: return "quarantine";
    case FileStorageState::Managed: return "managed";
    case FileStorageState::Missing: return "missing";
    case FileStorageState::Corrupt: return "corrupt";
    }
    return "quarantine";
}

inline std::optional<FileStorageState> fileStorageStateFrom(std::string_view value)
{
    if (value == "quarantine") return FileStorageState::Quarantine;
    if (value == "managed") return FileStorageState::Managed;
    if (value == "missing") return FileStorageState::Missing;
    if (value == "corrupt") return FileStorageState::Corrupt;
    return std::nullopt;
}

struct FileAsset
{
    Uid uid;
    std::optional<Uid> sourceId;
    std::string relativePath;         // 受管仓库内相对路径（业务不直接拼接绝对路径）
    std::string mimeType;
    qint64 byteSize = 0;
    std::string sha256;               // 内容指纹，UNIQUE（重复检测）
    std::string originalName;
    FileStorageState storageState = FileStorageState::Quarantine;
    std::string importedAt;
    int revision = 1;

    bool isValid() const
    {
        return !uid.empty() && !relativePath.empty() && !mimeType.empty()
               && !sha256.empty();
    }
};

} // namespace PersonOS::Domain
