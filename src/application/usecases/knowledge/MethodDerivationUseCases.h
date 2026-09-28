#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/KnowledgeRepository.h"
#include "application/ports/SearchIndexPort.h"
#include "application/ports/UuidPort.h"
#include "domain/foundation/Clock.h"
#include "domain/knowledge/Knowledge.h"

// 论文转方法（DD-001 §5.2 DeriveMethods；DR-004/010/011/012；requirements R4.4.1）
// 纪律：
// - 方法从论文证据推导，建立 derived_from 关系与证据链接；
// - 高度相似方法合并（保留各来源证据关系，可撤销，不物理删除）；
// - 新证据补充生成新版本（追加），实质变化创建新方法（evolves/supersedes）；
// - 入库与采用分离：方法入库不要求用户逐条批准，用户采用与否另行决定。
namespace PersonOS::Application {

class MethodDerivationUseCases
{
public:
    struct MethodCandidate
    {
        std::string title;
        std::string summary;
        std::vector<Domain::MethodStep> steps;
        std::string riskLevel = "low";
        std::string evidenceGrade = "B";
    };

    struct AdmitInput
    {
        Domain::Uid paperItemUid;         // 来源论文（可多篇调用多次）
        std::vector<MethodCandidate> candidates;
        std::string createdBy = "ai";
    };

    struct AdmitOutput
    {
        std::vector<Domain::KnowledgeItem> admitted;      // 新入库或合并后的方法条目
        std::vector<std::string> mergedIntoTitles;        // 被合并的既有方法标题
    };

    MethodDerivationUseCases(KnowledgeRepository &repo, SearchIndexPort &searchIndex,
                             UuidPort &uids, const Domain::Clock &clock);

    // 判定并入库：候选方法经最低可信度与可追溯性检查（来源论文存在、步骤非空），
    // 与既有同标题方法自动合并（保留来源关系，可撤销）
    Result<AdmitOutput, ApplicationError> admit(const AdmitInput &input);

    // 新证据补充：既有方法追加新版本（不覆盖旧版本）
    struct VersionInput
    {
        std::string summary;
        std::vector<Domain::MethodStep> steps;
        std::string createdBy = "ai";
    };
    Result<Domain::KnowledgeVersion, ApplicationError> supplement(
        const Domain::Uid &methodItemUid, int expectedRevision, const VersionInput &input);

private:
    KnowledgeRepository &m_repo;
    SearchIndexPort &m_searchIndex;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
