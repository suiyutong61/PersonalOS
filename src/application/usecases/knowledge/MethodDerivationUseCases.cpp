#include "application/usecases/knowledge/MethodDerivationUseCases.h"

#include "application/audit/Audit.h"

namespace PersonOS::Application {

namespace {
std::string joinSteps(const std::vector<Domain::MethodStep> &steps)
{
    std::string text;
    for (const auto &step : steps) {
        if (!text.empty())
            text += "\n";
        text += step.instruction;
    }
    return text;
}
} // namespace

MethodDerivationUseCases::MethodDerivationUseCases(KnowledgeRepository &repo,
                                                   SearchIndexPort &searchIndex,
                                                   UuidPort &uids,
                                                   const Domain::Clock &clock)
    : m_repo(repo), m_searchIndex(searchIndex), m_uids(uids), m_clock(clock)
{}

Result<MethodDerivationUseCases::AdmitOutput, ApplicationError>
MethodDerivationUseCases::admit(const AdmitInput &input)
{
    // 最低可信度与可追溯性检查：来源论文必须存在（追溯链成立）
    const auto paper = m_repo.findItem(input.paperItemUid);
    if (!paper || paper->libraryType != Domain::LibraryType::Paper)
        return Result<AdmitOutput, ApplicationError>::failure(
            {ErrorCode::Validation, "source paper not found", {}, false});

    AdmitOutput output;
    for (const auto &candidate : input.candidates) {
        if (candidate.title.empty() || candidate.steps.empty())
            return Result<AdmitOutput, ApplicationError>::failure(
                {ErrorCode::Validation, "method candidate must have title and steps", {},
                 false});

        // 相似检测（v1 确定性规则：同标题视为高度相似 → 合并）
        std::optional<Domain::KnowledgeItem> existing;
        for (const auto &item : m_repo.findItemsByTitle(candidate.title))
            if (item.libraryType == Domain::LibraryType::Method
                && item.status != Domain::KnowledgeStatus::Archived)
                existing = item;
        if (existing) {
            // 合并：新来源建立独立证据关系（derived_from），原方法归档状态表达
            //（不物理删除；撤销能力由关系与状态保留）
            Domain::KnowledgeRelation sourceRelation;
            sourceRelation.fromItemUid = existing->uid.value();
            sourceRelation.toItemUid = input.paperItemUid.value();
            sourceRelation.relation = "derived_from";
            sourceRelation.confidence = 1.0;
            // 幂等：同来源关系已存在则跳过（UNIQUE 约束兜底）
            if (!m_repo.relationExists(sourceRelation)) {
                const auto related = m_repo.insertRelation(sourceRelation);
                if (!related.ok)
                    return Result<AdmitOutput, ApplicationError>::failure(related.error);
            }
            output.mergedIntoTitles.push_back(candidate.title);
            continue;
        }

        // 新方法入库：条目 + 版本 + 方法详情 + 步骤 + derived_from 关系
        Domain::KnowledgeItem item;
        item.uid = m_uids.next();
        item.libraryType = Domain::LibraryType::Method;
        item.title = candidate.title;
        item.domainCode = paper->domainCode;
        item.status = Domain::KnowledgeStatus::Active;
        item.ownerScope = "system";
        item.createdBy = input.createdBy;
        item.neverDelete = true;   // DR-012
        const auto itemSaved = m_repo.insertItem(item);
        if (!itemSaved.ok)
            return Result<AdmitOutput, ApplicationError>::failure(itemSaved.error);

        Domain::KnowledgeVersion version;
        version.uid = m_uids.next();
        version.itemUid = item.uid;
        version.versionNo = 1;
        version.status = Domain::KnowledgeStatus::Active;
        version.summary = candidate.summary;
        version.contentHash = "derived:" + input.paperItemUid.value() + ":"
                              + candidate.title;
        version.validFrom = m_clock.utcIso();
        version.createdBy = input.createdBy;
        const auto versionSaved = m_repo.insertVersion(version);
        if (!versionSaved)
            return Result<AdmitOutput, ApplicationError>::failure(versionSaved.error());
        const std::string versionUid = versionSaved.value();

        Domain::MethodDetail detail;
        detail.itemUid = item.uid;
        detail.methodType = "derived_technique";
        detail.riskLevel = candidate.riskLevel;
        detail.evidenceGrade = candidate.evidenceGrade;
        const auto detailSaved = m_repo.insertMethodDetail(detail);
        if (!detailSaved.ok)
            return Result<AdmitOutput, ApplicationError>::failure(detailSaved.error);
        for (const auto &step : candidate.steps) {
            Domain::MethodStep bound = step;
            bound.methodVersionUid = *Domain::Uid::parse(versionUid);
            const auto stepSaved = m_repo.insertMethodStep(bound);
            if (!stepSaved.ok)
                return Result<AdmitOutput, ApplicationError>::failure(stepSaved.error);
        }

        item.currentVersionUid = versionUid;
        const auto itemUpdated = m_repo.updateItem(item, item.revision);
        if (!itemUpdated.ok)
            return Result<AdmitOutput, ApplicationError>::failure(itemUpdated.error);

        Domain::KnowledgeRelation sourceRelation;
        sourceRelation.fromItemUid = item.uid.value();
        sourceRelation.toItemUid = input.paperItemUid.value();
        sourceRelation.relation = "derived_from";
        sourceRelation.confidence = 1.0;
        const auto related = m_repo.insertRelation(sourceRelation);
        if (!related.ok)
            return Result<AdmitOutput, ApplicationError>::failure(related.error);

        // 索引同步
        const auto indexed = m_searchIndex.indexVersion(
            "method", item.uid.value(), candidate.title, candidate.summary, "[]", "{}", "[]",
            joinSteps(candidate.steps));
        if (!indexed.ok)
            return Result<AdmitOutput, ApplicationError>::failure(indexed.error);

        output.admitted.push_back(std::move(item));
    }
    Audit::record({"ai", {}, "method.derived", "knowledge",
                   output.admitted.empty() ? std::string() : output.admitted.front().uid.value(),
                   "{\"count\":" + std::to_string(output.admitted.size()) + "}"});
    return Result<AdmitOutput, ApplicationError>::success(std::move(output));
}

Result<Domain::KnowledgeVersion, ApplicationError> MethodDerivationUseCases::supplement(
    const Domain::Uid &methodItemUid, int expectedRevision, const VersionInput &input)
{
    const auto item = m_repo.findItem(methodItemUid);
    if (!item || item->libraryType != Domain::LibraryType::Method)
        return Result<Domain::KnowledgeVersion, ApplicationError>::failure(
            {ErrorCode::NotFound, "method item not found", {}, false});

    const auto versions = m_repo.versionsOf(methodItemUid);
    int nextNo = 1;
    for (const auto &version : versions)
        nextNo = std::max(nextNo, version.versionNo + 1);

    Domain::KnowledgeVersion version;
    version.uid = m_uids.next();
    version.itemUid = methodItemUid;
    version.versionNo = nextNo;
    version.status = Domain::KnowledgeStatus::Draft;   // 新证据版本待确认后激活（版本状态集不含 candidate）
    version.summary = input.summary;
    version.contentHash = "supplement:" + methodItemUid.value() + ":"
                          + std::to_string(nextNo);
    version.validFrom = m_clock.utcIso();
    version.createdBy = input.createdBy;
    const auto saved = m_repo.insertVersion(version);
    if (!saved)
        return Result<Domain::KnowledgeVersion, ApplicationError>::failure(saved.error());

    for (const auto &step : input.steps) {
        Domain::MethodStep bound = step;
        bound.methodVersionUid = *Domain::Uid::parse(saved.value());
        const auto stepSaved = m_repo.insertMethodStep(bound);
        if (!stepSaved.ok)
            return Result<Domain::KnowledgeVersion, ApplicationError>::failure(
                stepSaved.error);
    }
    Audit::record({"ai", {}, "method.version_added", "knowledge", methodItemUid.value(),
                   "{\"version\":" + std::to_string(version.versionNo) + "}"});
    return Result<Domain::KnowledgeVersion, ApplicationError>::success(std::move(version));
}

} // namespace PersonOS::Application
