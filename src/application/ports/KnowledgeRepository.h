#pragma once

#include <optional>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/GoalRepository.h"   // SaveResult
#include "domain/foundation/Uid.h"
#include "domain/knowledge/Knowledge.h"

// 知识仓储端口（DD-001 §5.3；五库共享骨架 + 类型专用表）
namespace PersonOS::Application {

class KnowledgeRepository
{
public:
    virtual ~KnowledgeRepository() = default;

    // 多表知识写入的原子边界；实现必须支持失败回滚。
    virtual SaveResult beginWrite() = 0;
    virtual SaveResult commitWrite() = 0;
    virtual void rollbackWrite() = 0;

    // 知识条目（更新走 revision；删除只允许状态变更，不提供物理删除）
    virtual std::optional<Domain::KnowledgeItem> findItem(const Domain::Uid &uid) = 0;
    virtual std::vector<Domain::KnowledgeItem> findItemsByTitle(const std::string &title) = 0;
    // 最近条目（知识库页只读投影）
    virtual std::vector<Domain::KnowledgeItem> listRecent(int limit) = 0;
    virtual SaveResult insertItem(const Domain::KnowledgeItem &item) = 0;
    virtual SaveResult updateItem(const Domain::KnowledgeItem &item, int expectedRevision) = 0;

    // 版本（append-only；成功返回版本 UID）
    virtual Result<std::string, ApplicationError> insertVersion(
        const Domain::KnowledgeVersion &version) = 0;
    virtual std::vector<Domain::KnowledgeVersion> versionsOf(const Domain::Uid &itemUid) = 0;

    // 来源与证据
    virtual SaveResult insertSource(const Domain::SourceRecord &source) = 0;
    virtual SaveResult insertFragment(const Domain::EvidenceFragment &fragment) = 0;
    virtual SaveResult insertEvidenceLink(const Domain::EvidenceLink &link) = 0;

    // 知识间关系（有类型；自关系由数据库 CHECK 兜底）
    virtual SaveResult insertRelation(const Domain::KnowledgeRelation &relation) = 0;
    virtual bool relationExists(const Domain::KnowledgeRelation &relation) = 0;
    virtual std::vector<Domain::KnowledgeRelation> relationsOf(const Domain::Uid &itemUid) = 0;

    // 类型专用
    virtual SaveResult insertPaperDetail(const Domain::PaperDetail &detail) = 0;
    virtual SaveResult insertMethodDetail(const Domain::MethodDetail &detail) = 0;
    virtual SaveResult insertTipDetail(const Domain::TipDetail &detail) = 0;
    virtual SaveResult insertPlanDetail(const Domain::PlanDetail &detail) = 0;
    virtual SaveResult insertMethodStep(const Domain::MethodStep &step) = 0;
};

} // namespace PersonOS::Application
