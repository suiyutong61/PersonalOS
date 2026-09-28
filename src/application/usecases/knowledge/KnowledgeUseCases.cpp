#include "application/usecases/knowledge/KnowledgeUseCases.h"

#include "application/audit/Audit.h"

namespace PersonOS::Application {

namespace {
class ImportTransaction
{
public:
    explicit ImportTransaction(KnowledgeRepository &repository) : m_repository(repository) {}
    ~ImportTransaction() { if (m_active) m_repository.rollbackWrite(); }
    std::optional<ApplicationError> begin()
    {
        const auto result = m_repository.beginWrite();
        if (!result.ok) return result.error;
        m_active = true;
        return std::nullopt;
    }
    std::optional<ApplicationError> commit()
    {
        const auto result = m_repository.commitWrite();
        if (!result.ok) return result.error;
        m_active = false;
        return std::nullopt;
    }
private:
    KnowledgeRepository &m_repository;
    bool m_active = false;
};

// 方法步骤聚合为可检索文本（FTS 索引用）
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

KnowledgeUseCases::KnowledgeUseCases(KnowledgeRepository &repo, SearchIndexPort &searchIndex,
                                     UuidPort &uids, const Domain::Clock &clock)
    : m_repo(repo), m_searchIndex(searchIndex), m_uids(uids), m_clock(clock)
{}

Result<KnowledgeUseCases::ImportOutput, ApplicationError> KnowledgeUseCases::importKnowledge(
    const ImportInput &input)
{
    if (input.contentHash.empty())
        return Result<ImportOutput, ApplicationError>::failure(
            {ErrorCode::Validation, "content hash required", {}, false});
    if (!input.source && input.createdBy != "generated")
        return Result<ImportOutput, ApplicationError>::failure(
            {ErrorCode::Validation, "source required for non-generated import", {}, false});

    ImportTransaction transaction(m_repo);
    if (const auto error = transaction.begin())
        return Result<ImportOutput, ApplicationError>::failure(*error);

    // 条目：论文与方法强制不可删除（DR-012；数据库 CHECK 兜底）
    Domain::KnowledgeItem item;
    item.uid = m_uids.next();
    item.libraryType = input.libraryType;
    item.title = input.title;
    item.domainCode = input.domainCode;
    item.status = input.initialStatus;
    item.ownerScope = input.createdBy == "system" || input.createdBy == "developer"
                          ? std::string("system")
                          : std::string("user");
    item.createdBy = input.createdBy;
    item.neverDelete = input.libraryType == Domain::LibraryType::Paper
                       || input.libraryType == Domain::LibraryType::Method;
    if (!item.isValid())
        return Result<ImportOutput, ApplicationError>::failure(
            {ErrorCode::Validation, "knowledge item invalid", {}, false});

    const auto itemSaved = m_repo.insertItem(item);
    if (!itemSaved.ok)
        return Result<ImportOutput, ApplicationError>::failure(itemSaved.error);

    // 来源（若有）
    std::optional<Domain::Uid> sourceUid;
    if (input.source) {
        Domain::SourceRecord source = *input.source;
        source.uid = m_uids.next();
        source.accessedAt = source.accessedAt.empty() ? m_clock.utcIso() : source.accessedAt;
        if (!source.isValid())
            return Result<ImportOutput, ApplicationError>::failure(
                {ErrorCode::Validation, "source invalid", {}, false});
        const auto sourceSaved = m_repo.insertSource(source);
        if (!sourceSaved.ok)
            return Result<ImportOutput, ApplicationError>::failure(sourceSaved.error);
        sourceUid = source.uid;
    }

    // 首个版本
    Domain::KnowledgeVersion version;
    version.uid = m_uids.next();
    version.itemUid = item.uid;
    version.versionNo = 1;
    version.status = input.initialStatus == Domain::KnowledgeStatus::Active
                         ? Domain::KnowledgeStatus::Active
                         : Domain::KnowledgeStatus::Draft;
    version.summary = input.summary;
    version.claimsJson = input.claimsJson;
    version.applicabilityJson = input.applicabilityJson;
    version.limitationsJson = input.limitationsJson;
    version.warningJson = input.warningJson;
    version.contentHash = input.contentHash;
    version.language = input.language;
    version.extractionModel = input.extractionModel;
    version.extractionPromptVersion = input.extractionPromptVersion;
    version.validFrom = m_clock.utcIso();
    version.createdBy = input.createdBy;
    const auto versionSaved = m_repo.insertVersion(version);
    if (!versionSaved)
        return Result<ImportOutput, ApplicationError>::failure(versionSaved.error());
    const std::string versionUid = versionSaved.value();

    // 条目指向当前版本
    item.currentVersionUid = versionUid;
    const auto itemUpdated = m_repo.updateItem(item, item.revision);
    if (!itemUpdated.ok)
        return Result<ImportOutput, ApplicationError>::failure(itemUpdated.error);

    // 全文索引同步（导入成功 ⇒ 已索引；失败时整体失败，rebuildAll 为修复路径）
    const auto indexed = m_searchIndex.indexVersion(
        Domain::toString(input.libraryType), item.uid.value(), input.title, input.summary,
        input.claimsJson, input.applicabilityJson, input.limitationsJson,
        joinSteps(input.methodSteps));
    if (!indexed.ok)
        return Result<ImportOutput, ApplicationError>::failure(indexed.error);

    // 类型详情（回填条目 UID；方法步骤绑定本版本）
    if (input.paper) {
        Domain::PaperDetail detail = *input.paper;
        detail.itemUid = item.uid;
        const auto saved = m_repo.insertPaperDetail(detail);
        if (!saved.ok)
            return Result<ImportOutput, ApplicationError>::failure(saved.error);
    }
    if (input.method) {
        Domain::MethodDetail detail = *input.method;
        detail.itemUid = item.uid;
        const auto saved = m_repo.insertMethodDetail(detail);
        if (!saved.ok)
            return Result<ImportOutput, ApplicationError>::failure(saved.error);
        for (const auto &step : input.methodSteps) {
            Domain::MethodStep bound = step;
            bound.methodVersionUid = *Domain::Uid::parse(versionUid);
            const auto stepSaved = m_repo.insertMethodStep(bound);
            if (!stepSaved.ok)
                return Result<ImportOutput, ApplicationError>::failure(stepSaved.error);
        }
    }
    if (input.tip) {
        Domain::TipDetail detail = *input.tip;
        detail.itemUid = item.uid;
        const auto saved = m_repo.insertTipDetail(detail);
        if (!saved.ok)
            return Result<ImportOutput, ApplicationError>::failure(saved.error);
    }
    if (input.plan) {
        Domain::PlanDetail detail = *input.plan;
        detail.itemUid = item.uid;
        const auto saved = m_repo.insertPlanDetail(detail);
        if (!saved.ok)
            return Result<ImportOutput, ApplicationError>::failure(saved.error);
    }

    ImportOutput output;
    output.item = std::move(item);
    output.versionUid = versionUid;
    if (sourceUid)
        output.sourceUid = sourceUid->value();
    if (const auto error = transaction.commit())
        return Result<ImportOutput, ApplicationError>::failure(*error);
    Audit::record({"user", {}, "knowledge.imported", "knowledge", item.uid.value(),
                   "{\"title\":\"" + item.title + "\"}"});
    return Result<ImportOutput, ApplicationError>::success(std::move(output));
}

Result<Domain::KnowledgeVersion, ApplicationError> KnowledgeUseCases::addVersion(
    const Domain::Uid &itemUid, int expectedRevision, const VersionInput &input)
{
    const auto item = m_repo.findItem(itemUid);
    if (!item)
        return Result<Domain::KnowledgeVersion, ApplicationError>::failure(
            {ErrorCode::NotFound, "knowledge item not found", {}, false});
    if (input.contentHash.empty())
        return Result<Domain::KnowledgeVersion, ApplicationError>::failure(
            {ErrorCode::Validation, "content hash required", {}, false});

    const auto versions = m_repo.versionsOf(itemUid);
    int nextNo = 1;
    for (const auto &version : versions)
        nextNo = std::max(nextNo, version.versionNo + 1);

    Domain::KnowledgeVersion version;
    version.uid = m_uids.next();
    version.itemUid = itemUid;
    version.versionNo = nextNo;
    version.status = input.isActive ? Domain::KnowledgeStatus::Active
                                    : Domain::KnowledgeStatus::Draft;
    version.summary = input.summary;
    version.claimsJson = input.claimsJson;
    version.applicabilityJson = input.applicabilityJson;
    version.limitationsJson = input.limitationsJson;
    version.warningJson = input.warningJson;
    version.contentHash = input.contentHash;
    version.extractionModel = input.extractionModel;
    version.extractionPromptVersion = input.extractionPromptVersion;
    version.validFrom = m_clock.utcIso();
    version.createdBy = input.createdBy;
    ImportTransaction transaction(m_repo);
    if (const auto error = transaction.begin())
        return Result<Domain::KnowledgeVersion, ApplicationError>::failure(*error);
    const auto saved = m_repo.insertVersion(version);
    if (!saved)
        return Result<Domain::KnowledgeVersion, ApplicationError>::failure(saved.error());

    if (input.isActive) {
        Domain::KnowledgeItem updated = *item;
        updated.currentVersionUid = saved.value();
        const auto itemSaved = m_repo.updateItem(updated, expectedRevision);
        if (!itemSaved.ok)
            return Result<Domain::KnowledgeVersion, ApplicationError>::failure(
                itemSaved.error);
        // 索引同步到新版本内容
        const auto indexed = m_searchIndex.indexVersion(
            Domain::toString(item->libraryType), itemUid.value(), item->title,
            input.summary, input.claimsJson, input.applicabilityJson, input.limitationsJson,
            "");
        if (!indexed.ok)
            return Result<Domain::KnowledgeVersion, ApplicationError>::failure(
                indexed.error);
    }
    if (const auto error = transaction.commit())
        return Result<Domain::KnowledgeVersion, ApplicationError>::failure(*error);
    Audit::record({"user", {}, "knowledge.version_added", "knowledge", itemUid.value(),
                   "{\"version\":" + std::to_string(version.versionNo) + "}"});
    return Result<Domain::KnowledgeVersion, ApplicationError>::success(std::move(version));
}

Result<Domain::KnowledgeItem, ApplicationError> KnowledgeUseCases::deprecate(
    const Domain::Uid &itemUid, int expectedRevision, Domain::KnowledgeStatus status)
{
    if (status != Domain::KnowledgeStatus::Archived
        && status != Domain::KnowledgeStatus::Superseded
        && status != Domain::KnowledgeStatus::Warned)
        return Result<Domain::KnowledgeItem, ApplicationError>::failure(
            {ErrorCode::Validation, "invalid deprecation status", {}, false});
    const auto item = m_repo.findItem(itemUid);
    if (!item)
        return Result<Domain::KnowledgeItem, ApplicationError>::failure(
            {ErrorCode::NotFound, "knowledge item not found", {}, false});
    Domain::KnowledgeItem updated = *item;
    updated.status = status;
    const auto saved = m_repo.updateItem(updated, expectedRevision);
    if (!saved.ok)
        return Result<Domain::KnowledgeItem, ApplicationError>::failure(saved.error);
    updated.revision = expectedRevision + 1;
    Audit::record({"user", {}, "knowledge.deprecated", "knowledge", updated.uid.value(), "{}"});
    return Result<Domain::KnowledgeItem, ApplicationError>::success(std::move(updated));
}

Result<Domain::KnowledgeRelation, ApplicationError> KnowledgeUseCases::relate(
    const Domain::KnowledgeRelation &relation)
{
    if (!relation.isValid())
        return Result<Domain::KnowledgeRelation, ApplicationError>::failure(
            {ErrorCode::Validation, "knowledge relation invalid", {}, false});
    const auto saved = m_repo.insertRelation(relation);
    if (!saved.ok)
        return Result<Domain::KnowledgeRelation, ApplicationError>::failure(saved.error);
    return Result<Domain::KnowledgeRelation, ApplicationError>::success(relation);
}

Result<Domain::EvidenceLink, ApplicationError> KnowledgeUseCases::linkEvidence(
    const Domain::EvidenceLink &link)
{
    if (!link.isValid())
        return Result<Domain::EvidenceLink, ApplicationError>::failure(
            {ErrorCode::Validation, "evidence link invalid", {}, false});
    const auto saved = m_repo.insertEvidenceLink(link);
    if (!saved.ok)
        return Result<Domain::EvidenceLink, ApplicationError>::failure(saved.error);
    return Result<Domain::EvidenceLink, ApplicationError>::success(link);
}

} // namespace PersonOS::Application
