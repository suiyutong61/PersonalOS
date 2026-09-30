#pragma once

#include <string>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/GoalRepository.h"   // SaveResult

// 全文检索索引端口（DD-001 数据库设计 §6：索引服务负责同步，提供全量重建
// 与一致性检查；向量索引另由 EmbeddingPort 承担，IMP-005）
namespace PersonOS::Application {

struct SearchHit
{
    std::string ownerType;    // paper/method/tip/plan
    std::string ownerUid;
    std::string title;
    std::string summary;
};

class SearchIndexPort
{
public:
    virtual ~SearchIndexPort() = default;

    // 以知识版本内容建立/更新索引（owner 维度覆盖式更新）
    virtual SaveResult indexVersion(const std::string &ownerType,
                                    const std::string &ownerUid, const std::string &title,
                                    const std::string &summary, const std::string &claims,
                                    const std::string &applicability,
                                    const std::string &limitations,
                                    const std::string &steps) = 0;

    // 全文检索（FTS5 MATCH）
    virtual std::vector<SearchHit> search(const std::string &query, int limit) = 0;

    // 全量重建（由正式表派生；索引损坏时可用）
    virtual SaveResult rebuildAll() = 0;

    // 一致性检查：索引条目数 vs 正式版本数
    virtual bool isConsistent() = 0;

    // 移除某知识条目的全部索引行（候选条目物理删除时同步）
    virtual SaveResult removeOwner(const std::string &ownerUid) = 0;
};

} // namespace PersonOS::Application
