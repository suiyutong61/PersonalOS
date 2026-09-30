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
    // 按内容指纹查找来源（论文全文只存受管原件，分析时经 content_hash 定位来源）
    virtual std::optional<Domain::SourceRecord> findSourceByContentHash(
        const std::string &contentHash) = 0;
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

    // 详情读取(知识库详情视图)
    virtual std::optional<Domain::MethodDetail> methodDetailOf(const Domain::Uid &itemUid) = 0;
    virtual std::vector<Domain::MethodStep> methodStepsOf(const Domain::Uid &versionUid) = 0;
    virtual std::vector<Domain::EvidenceFragment> evidenceFragmentsOf(
        const Domain::Uid &versionUid) = 0;

    // 级联物理删除(仅限 AI 生成候选;调用方负责资格校验)。
    // 同一事务:类型详情→关系→版本(级联步骤/适用性)→条目(级联分类/标签)。
    virtual SaveResult deleteItemCascade(const Domain::Uid &uid) = 0;

    // 编号辅助(基于 Research Base 01-43):某领域内论文数(用户导入自动编号),
    // 某论文派生出的某类型候选数(衍生方法/贴士编号)
    virtual int countPapersInDomain(const std::string &domainCode) = 0;
    virtual int derivedCandidateCount(const Domain::Uid &paperItemUid,
                                      Domain::LibraryType type) = 0;
};

} // namespace PersonOS::Application
