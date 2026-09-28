#include "application/usecases/knowledge/PaperAnalysisUseCases.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace PersonOS::Application {
namespace {

std::string compact(const QJsonValue &value)
{
    if (value.isArray())
        return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact).toStdString();
    if (value.isObject())
        return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact).toStdString();
    return "{}";
}

ApplicationError validation(const std::string &message)
{
    return {ErrorCode::Validation, message, {}, false};
}

class WriteTransaction
{
public:
    explicit WriteTransaction(KnowledgeRepository &repository) : m_repository(repository) {}
    ~WriteTransaction() { if (m_active) m_repository.rollbackWrite(); }
    Result<bool, ApplicationError> begin()
    {
        const auto result = m_repository.beginWrite();
        if (!result.ok)
            return Result<bool, ApplicationError>::failure(result.error);
        m_active = true;
        return Result<bool, ApplicationError>::success(true);
    }
    Result<bool, ApplicationError> commit()
    {
        const auto result = m_repository.commitWrite();
        if (!result.ok)
            return Result<bool, ApplicationError>::failure(result.error);
        m_active = false;
        return Result<bool, ApplicationError>::success(true);
    }
private:
    KnowledgeRepository &m_repository;
    bool m_active = false;
};

QVector<QString> splitText(const QString &text, int maximum, int overlap)
{
    QVector<QString> chunks;
    maximum = std::max(1000, maximum);
    overlap = std::clamp(overlap, 0, maximum / 4);
    int start = 0;
    while (start < text.size()) {
        int end = std::min(start + maximum, static_cast<int>(text.size()));
        if (end < text.size()) {
            const int paragraph = text.lastIndexOf(QStringLiteral("\n\n"), end);
            if (paragraph > start + maximum / 2)
                end = paragraph;
        }
        chunks.push_back(text.mid(start, end - start));
        if (end >= text.size())
            break;
        start = std::max(start + 1, end - overlap);
    }
    return chunks;
}

void appendArray(QJsonObject &target, const QString &key, const QJsonArray &values,
                 int chunkIndex)
{
    QJsonArray combined = target.value(key).toArray();
    for (const auto &value : values) {
        if (key == QStringLiteral("evidence_fragments")) {
            QJsonObject object = value.toObject();
            QJsonObject locator = object.value(QStringLiteral("locator")).toObject();
            locator.insert(QStringLiteral("chunk_index"), chunkIndex);
            object.insert(QStringLiteral("locator"), locator);
            combined.append(object);
        } else {
            combined.append(value);
        }
    }
    target.insert(key, combined);
}

} // namespace

PaperAnalysisUseCases::PaperAnalysisUseCases(AiGatewayPort &gateway,
                                             KnowledgeRepository &repository,
                                             KnowledgeUseCases &knowledge,
                                             UuidPort &uids,
                                             const Domain::Clock &clock)
    : m_gateway(gateway), m_repository(repository), m_knowledge(knowledge),
      m_uids(uids), m_clock(clock)
{}

Result<PaperAnalysisUseCases::AnalyzeOutput, ApplicationError>
PaperAnalysisUseCases::analyze(const AnalyzeInput &input)
{
    const auto paper = m_repository.findItem(input.paperItemUid);
    if (!paper || paper->libraryType != Domain::LibraryType::Paper)
        return Result<AnalyzeOutput, ApplicationError>::failure(
            validation("paper item not found"));
    if (input.extractedText.empty() || input.analysisContentHash.empty()
        || input.idempotencyKey.empty())
        return Result<AnalyzeOutput, ApplicationError>::failure(
            validation("paper text, analysis hash and idempotency key are required"));

    const auto chunks = splitText(QString::fromStdString(input.extractedText),
                                  input.maxChunkCharacters, input.chunkOverlapCharacters);
    QJsonObject analysis;
    QStringList summaries;
    for (int chunkIndex = 0; chunkIndex < chunks.size(); ++chunkIndex) {
        QJsonObject request;
        request.insert(QStringLiteral("title"), QString::fromStdString(input.title));
        request.insert(QStringLiteral("paper_text"), chunks.at(chunkIndex));
        request.insert(QStringLiteral("chunk_index"), chunkIndex);
        request.insert(QStringLiteral("chunk_count"), chunks.size());
        request.insert(QStringLiteral("instructions"), QStringLiteral(
            "Analyze only this supplied paper chunk. Mark unknowns explicitly. Every claim, "
            "method and tip must cite an evidence_fragments entry with a verbatim excerpt and "
            "locator. Do not infer content from missing chunks."));
        AiGatewaySubmit submit;
        submit.providerConfigUid = input.providerConfigUid;
        submit.jobType = "paper_analysis";
        submit.contractType = "paper_analysis_v1";
        submit.contractVersion = "1";
        submit.requestJson = QJsonDocument(request).toJson(QJsonDocument::Compact).toStdString();
        submit.idempotencyKey = input.idempotencyKey + ":chunk:" + std::to_string(chunkIndex);
        submit.sourceMode = Domain::SourceMode::KnowledgeGrounded;
        submit.maxAttempts = 3;
        submit.contextItems.push_back({"paper", input.paperItemUid.value(),
                                       paper->currentVersionUid.value_or(""), chunkIndex,
                                       "paper chunk being analyzed"});
        const auto registered = m_gateway.submit(submit);
        if (!registered)
            return Result<AnalyzeOutput, ApplicationError>::failure(registered.error());
        const auto executed = m_gateway.execute(registered.value().uid);
        if (!executed)
            return Result<AnalyzeOutput, ApplicationError>::failure(executed.error());
        if (executed.value().status != Domain::AiJobStatus::Completed
            || !executed.value().resultJson)
            return Result<AnalyzeOutput, ApplicationError>::failure(
                {ErrorCode::ExternalUnavailable, "paper analysis did not complete", {}, true});
        const QJsonDocument document = QJsonDocument::fromJson(
            QByteArray::fromStdString(*executed.value().resultJson));
        if (!document.isObject())
            return Result<AnalyzeOutput, ApplicationError>::failure(
                validation("paper analysis result is not an object"));
        const QJsonObject chunkAnalysis = document.object();
        const QString summary = chunkAnalysis.value(QStringLiteral("summary")).toString().trimmed();
        if (!summary.isEmpty()) summaries.push_back(summary);
        for (const QString &key : {QStringLiteral("core_claims"),
                                   QStringLiteral("applicability"),
                                   QStringLiteral("limitations"),
                                   QStringLiteral("candidate_methods"),
                                   QStringLiteral("candidate_tips"),
                                   QStringLiteral("evidence_fragments"),
                                   QStringLiteral("unknowns")})
            appendArray(analysis, key, chunkAnalysis.value(key).toArray(), chunkIndex);
        if (!analysis.contains(QStringLiteral("credibility")))
            analysis.insert(QStringLiteral("credibility"),
                            chunkAnalysis.value(QStringLiteral("credibility")));
    }
    analysis.insert(QStringLiteral("summary"), summaries.join(QStringLiteral("\n")));
    const QJsonArray evidenceValues = analysis.value(QStringLiteral("evidence_fragments")).toArray();
    bool hasUsableEvidence = false;
    for (const auto &value : evidenceValues)
        if (!value.toObject().value(QStringLiteral("text")).toString().trimmed().isEmpty())
            hasUsableEvidence = true;
    if (!hasUsableEvidence)
        return Result<AnalyzeOutput, ApplicationError>::failure(
            validation("paper analysis contains no usable evidence fragment"));

    WriteTransaction transaction(m_repository);
    const auto begun = transaction.begin();
    if (!begun)
        return Result<AnalyzeOutput, ApplicationError>::failure(begun.error());

    KnowledgeUseCases::VersionInput versionInput;
    versionInput.summary = analysis.value(QStringLiteral("summary"))
                               .toString(analysis.value(QStringLiteral("title")).toString())
                               .toStdString();
    versionInput.claimsJson = compact(analysis.value(QStringLiteral("core_claims")));
    versionInput.applicabilityJson = compact(analysis.value(QStringLiteral("applicability")));
    versionInput.limitationsJson = compact(analysis.value(QStringLiteral("limitations")));
    QJsonObject warnings;
    warnings.insert(QStringLiteral("credibility"), analysis.value(QStringLiteral("credibility")));
    warnings.insert(QStringLiteral("unknowns"), analysis.value(QStringLiteral("unknowns")));
    versionInput.warningJson = compact(warnings);
    versionInput.contentHash = input.analysisContentHash;
    versionInput.createdBy = "ai";
    versionInput.extractionModel = input.modelVersion;
    versionInput.extractionPromptVersion = input.promptVersion;
    versionInput.isActive = false; // AI analysis stays draft until an explicit promotion decision.
    const auto version = m_knowledge.addVersion(input.paperItemUid, paper->revision, versionInput);
    if (!version)
        return Result<AnalyzeOutput, ApplicationError>::failure(version.error());

    AnalyzeOutput output;
    output.paperVersion = version.value();
    output.analyzedChunkCount = chunks.size();
    output.rawAnalysisJson = QJsonDocument(analysis).toJson(QJsonDocument::Compact).toStdString();

    for (const auto &value : evidenceValues) {
        const QJsonObject object = value.toObject();
        const QString text = object.value(QStringLiteral("text")).toString().trimmed();
        if (text.isEmpty())
            continue;
        Domain::EvidenceFragment fragment;
        fragment.uid = m_uids.next();
        fragment.sourceUid = input.sourceUid;
        if (input.sourceAssetUid)
            fragment.assetUid = input.sourceAssetUid->value();
        fragment.text = text.toStdString();
        fragment.locatorJson = compact(object.value(QStringLiteral("locator")));
        fragment.fragmentHash = object.value(QStringLiteral("hash")).toString(
            fragment.uid.value().c_str()).toStdString();
        const auto saved = m_repository.insertFragment(fragment);
        if (!saved.ok)
            return Result<AnalyzeOutput, ApplicationError>::failure(saved.error);
        Domain::EvidenceLink link;
        link.knowledgeVersionUid = version.value().uid.value();
        link.fragmentUid = fragment.uid.value();
        // 模型可能返回数据库约束之外的取值(如 "limits");规范化为合法枚举
        QString relation = object.value(QStringLiteral("relation"))
                               .toString(QStringLiteral("supports"));
        if (relation == QStringLiteral("limits") || relation == QStringLiteral("restricts")
            || relation == QStringLiteral("weakens"))
            relation = QStringLiteral("qualifies");
        if (relation != QStringLiteral("supports")
            && relation != QStringLiteral("qualifies")
            && relation != QStringLiteral("contradicts")
            && relation != QStringLiteral("derived_from"))
            relation = QStringLiteral("supports");
        link.relation = relation.toStdString();
        link.strength = std::clamp(object.value(QStringLiteral("strength")).toDouble(0.5),
                                   0.0, 1.0);
        const auto linked = m_repository.insertEvidenceLink(link);
        if (!linked.ok)
            return Result<AnalyzeOutput, ApplicationError>::failure(linked.error);
        ++output.evidenceFragmentCount;
    }
    const auto importCandidate = [&](const QJsonObject &candidate, Domain::LibraryType type)
        -> Result<Domain::KnowledgeItem, ApplicationError> {
        KnowledgeUseCases::ImportInput candidateInput;
        candidateInput.libraryType = type;
        candidateInput.title = candidate.value(QStringLiteral("title")).toString().toStdString();
        candidateInput.domainCode = paper->domainCode;
        candidateInput.summary = candidate.value(QStringLiteral("summary"))
                                     .toString(candidate.value(QStringLiteral("claim")).toString())
                                     .toStdString();
        candidateInput.contentHash = input.analysisContentHash + ":"
                                     + Domain::toString(type) + ":" + candidateInput.title;
        candidateInput.createdBy = "generated";
        candidateInput.initialStatus = Domain::KnowledgeStatus::Candidate;
        candidateInput.extractionModel = input.modelVersion;
        candidateInput.extractionPromptVersion = input.promptVersion;
        if (type == Domain::LibraryType::Method) {
            Domain::MethodDetail detail;
            detail.methodType = "ai_candidate";
            detail.riskLevel = candidate.value(QStringLiteral("risk_level"))
                                   .toString(QStringLiteral("unknown")).toStdString();
            detail.evidenceGrade = candidate.value(QStringLiteral("evidence_grade"))
                                      .toString(QStringLiteral("unknown")).toStdString();
            candidateInput.method = detail;
            int sequence = 0;
            for (const auto &stepValue : candidate.value(QStringLiteral("steps")).toArray()) {
                Domain::MethodStep step;
                step.sequenceNo = sequence++;
                step.instruction = stepValue.toString().toStdString();
                if (!step.instruction.empty()) candidateInput.methodSteps.push_back(step);
            }
        } else {
            Domain::TipDetail detail;
            detail.captureType = "paper_analysis";
            detail.verificationStatus = "candidate";
            detail.riskLevel = candidate.value(QStringLiteral("risk_level"))
                                   .toString(QStringLiteral("unknown")).toStdString();
            detail.useScenarioJson = compact(candidate.value(QStringLiteral("use_scenario")));
            candidateInput.tip = detail;
        }
        if (candidateInput.title.empty() || candidateInput.summary.empty())
            return Result<Domain::KnowledgeItem, ApplicationError>::failure(
                validation("candidate title and summary required"));
        const auto imported = m_knowledge.importKnowledge(candidateInput);
        if (!imported)
            return Result<Domain::KnowledgeItem, ApplicationError>::failure(imported.error());
        Domain::KnowledgeRelation relation;
        relation.fromItemUid = imported.value().item.uid.value();
        relation.toItemUid = input.paperItemUid.value();
        relation.relation = "derived_from";
        relation.confidence = 1.0;
        const auto related = m_repository.insertRelation(relation);
        if (!related.ok)
            return Result<Domain::KnowledgeItem, ApplicationError>::failure(related.error);
        return Result<Domain::KnowledgeItem, ApplicationError>::success(imported.value().item);
    };

    for (const auto &value : analysis.value(QStringLiteral("candidate_methods")).toArray()) {
        const auto imported = importCandidate(value.toObject(), Domain::LibraryType::Method);
        if (!imported) return Result<AnalyzeOutput, ApplicationError>::failure(imported.error());
        output.candidateMethods.push_back(imported.value());
    }
    for (const auto &value : analysis.value(QStringLiteral("candidate_tips")).toArray()) {
        const auto imported = importCandidate(value.toObject(), Domain::LibraryType::Tip);
        if (!imported) return Result<AnalyzeOutput, ApplicationError>::failure(imported.error());
        output.candidateTips.push_back(imported.value());
    }
    const auto committed = transaction.commit();
    if (!committed)
        return Result<AnalyzeOutput, ApplicationError>::failure(committed.error());
    return Result<AnalyzeOutput, ApplicationError>::success(std::move(output));
}

Result<PaperAnalysisUseCases::RetractOutput, ApplicationError>
PaperAnalysisUseCases::retract(const RetractInput &input)
{
    const auto paper = m_repository.findItem(input.paperItemUid);
    if (!paper || paper->libraryType != Domain::LibraryType::Paper)
        return Result<RetractOutput, ApplicationError>::failure(
            validation("paper item not found"));
    const auto versions = m_repository.versionsOf(input.paperItemUid);
    const auto version = std::find_if(versions.begin(), versions.end(), [&](const auto &value) {
        return value.uid == input.analysisVersionUid;
    });
    if (version == versions.end() || version->status != Domain::KnowledgeStatus::Draft
        || version->createdBy != "ai")
        return Result<RetractOutput, ApplicationError>::failure(
            validation("retraction requires an AI draft analysis version"));

    RetractOutput output;
    output.retainedAnalysisVersionUid = input.analysisVersionUid;
    for (const auto &candidateUid : input.candidateItemUids) {
        const auto candidate = m_repository.findItem(candidateUid);
        if (!candidate || candidate->status != Domain::KnowledgeStatus::Candidate
            || candidate->createdBy != "generated")
            return Result<RetractOutput, ApplicationError>::failure(
                validation("retraction candidate is missing or no longer pending"));
        Domain::KnowledgeRelation relation;
        relation.fromItemUid = candidateUid.value();
        relation.toItemUid = input.paperItemUid.value();
        relation.relation = "derived_from";
        relation.confidence = 1.0;
        if (!m_repository.relationExists(relation))
            return Result<RetractOutput, ApplicationError>::failure(
                validation("retraction candidate is not derived from this paper"));
    }
    for (const auto &candidateUid : input.candidateItemUids) {
        const auto candidate = m_repository.findItem(candidateUid);
        const auto archived = m_knowledge.deprecate(candidateUid, candidate->revision,
                                                     Domain::KnowledgeStatus::Archived);
        if (!archived)
            return Result<RetractOutput, ApplicationError>::failure(archived.error());
        ++output.archivedCandidateCount;
    }
    return Result<RetractOutput, ApplicationError>::success(std::move(output));
}

} // namespace PersonOS::Application
