#include "infrastructure/knowledge/CorpusPipeline.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTimer>
#include <QThread>
#include <QUrl>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include "application/usecases/knowledge/KnowledgeUseCases.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/knowledge/ManagedTextExtraction.h"
#include "infrastructure/knowledge/ReliablePdfTextExtractor.h"
#include "infrastructure/knowledge/SqlKnowledgeFtsIndex.h"
#include "infrastructure/persistence/SqlKnowledgeRepository.h"
#include "infrastructure/persistence/SqlFileAssetRepository.h"

namespace PersonOS::Infrastructure {

namespace {

// 去除 HTML 标签的极简正文提取（链接正文；不是渲染器）
std::string stripHtml(const std::string &html)
{
    std::string out;
    bool inTag = false;
    for (const char c : html) {
        if (c == '<') {
            inTag = true;
            continue;
        }
        if (c == '>') {
            inTag = false;
            out += ' ';
            continue;
        }
        if (!inTag)
            out += c;
    }
    return out;
}

// 同步 HTTP GET（脚手架批量场景；用户触发，不后台联网）
struct FetchResult
{
    std::optional<std::string> body;
    std::vector<CorpusPipeline::DownloadAttempt> attempts;
};

FetchResult fetchUrl(const std::string &url, int maxAttempts = 3, int timeoutMs = 60000)
{
    FetchResult result;
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        QNetworkAccessManager manager;
        QNetworkRequest request{QUrl(QString::fromStdString(url))};
        request.setTransferTimeout(timeoutMs);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setRawHeader(QByteArrayLiteral("User-Agent"),
                             QByteArrayLiteral("PersonalOS-CorpusScaffold/1.1"));
        QEventLoop loop;
        QNetworkReply *reply = manager.get(request);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(timeoutMs + 5000, &loop, &QEventLoop::quit);
        loop.exec();
        if (!reply->isFinished())
            reply->abort();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        CorpusPipeline::DownloadAttempt record;
        record.source = url;
        record.attempt = attempt;
        record.httpStatus = status;
        if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
            const QByteArray body = reply->readAll();
            reply->deleteLater();
            if (!body.isEmpty()) {
                record.success = true;
                result.attempts.push_back(record);
                result.body = std::string(body.constData(), static_cast<size_t>(body.size()));
                return result;
            }
            record.error = "empty response";
            record.retryable = true;
        } else {
            record.error = "HTTP " + std::to_string(status) + " "
                           + reply->errorString().toStdString();
            record.retryable = status == 0 || status == 408 || status == 429
                               || status >= 500;
            reply->deleteLater();
            result.attempts.push_back(record);
            if (!record.retryable)
                break;
            if (attempt < maxAttempts)
                QThread::msleep(static_cast<unsigned long>(250 * (1 << (attempt - 1))));
            continue;
        }
        result.attempts.push_back(record);
        if (attempt < maxAttempts)
            QThread::msleep(static_cast<unsigned long>(250 * (1 << (attempt - 1))));
    }
    return result;
}

std::string inferTitle(const std::string &pathOrUrl)
{
    const size_t slash = pathOrUrl.find_last_of("/\\");
    std::string name = slash == std::string::npos ? pathOrUrl : pathOrUrl.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos)
        name = name.substr(0, dot);
    return name;
}


// NCBI E-utilities 官方 elink：pubmed→pmc 解析 PMCID。只认 linkname=pubmed_pmc
// （pubmed_pmc_refs 是"引用该文的文章"，不代表本文有 PMCID）。失败静默返回空。
QString resolvePmcidViaElink(const QString &pmid)
{
    const QString url = QStringLiteral(
        "https://eutils.ncbi.nlm.nih.gov/entrez/eutils/elink.fcgi"
        "?dbfrom=pubmed&db=pmc&id=%1&retmode=json")
                            .arg(pmid);
    const auto fetched = fetchUrl(url.toStdString(), 1, 20000);
    if (!fetched.body)
        return {};
    const QJsonObject root =
        QJsonDocument::fromJson(QByteArray::fromStdString(*fetched.body)).object();
    for (const auto &setValue : root.value(QStringLiteral("linksets")).toArray()) {
        for (const auto &dbValue
             : setValue.toObject().value(QStringLiteral("linksetdbs")).toArray()) {
            const QJsonObject db = dbValue.toObject();
            if (db.value(QStringLiteral("linkname")).toString()
                != QStringLiteral("pubmed_pmc"))
                continue;
            const QString id = db.value(QStringLiteral("links")).toArray().first().toString();
            if (!id.isEmpty())
                return QStringLiteral("PMC%1").arg(id);
        }
    }
    return {};
}

} // namespace

CorpusPipeline::CorpusPipeline(QSqlDatabase database, Application::UuidPort &uids,
                               const Domain::Clock &clock, Application::EmbeddingPort *embeddings,
                               Application::EmbeddingVectorStore *vectorStore)
    : m_database(std::move(database)), m_uids(uids), m_clock(clock),
      m_embeddings(embeddings), m_vectorStore(vectorStore)
{}

std::string CorpusPipeline::sha256Of(const std::string &content)
{
    return QCryptographicHash::hash(QByteArray::fromStdString(content),
                                    QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}

CorpusPipeline::Outcome CorpusPipeline::process(const Item &item)
{
    Outcome outcome;

    // 1) 获取内容（多个下载地址按索引顺序回退；失败保留每次原因）
    std::string raw;
    std::vector<std::string> candidates = item.sourceCandidates;
    if (candidates.empty() && !item.source.empty())
        candidates.push_back(item.source);
    // PMC 主站的 /pdf/ 可能返回浏览器检查页；追加官方 BioC、Europe PMC 全文 XML
    // 与 Europe PMC 的公开 PDF 渲染端点。
    const QRegularExpression pmcPattern(QStringLiteral("/articles/(PMC\\d+)"),
                                        QRegularExpression::CaseInsensitiveOption);
    // PubMed 摘要页不是全文；先经 NCBI E-utilities 官方 elink 解析 PMCID，
    // 再追加 BioC/Europe PMC 官方全文端点，最后才尝试按 PMID 直连 Europe PMC。
    const QRegularExpression pubmedPattern(
        QStringLiteral("pubmed\\.ncbi\\.nlm\\.nih\\.gov/(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto appendCandidate = [&candidates](const std::string &candidate) {
        if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end())
            candidates.push_back(candidate);
    };
    const auto originalCandidates = candidates;
    for (const auto &candidate : originalCandidates) {
        const auto pmcMatch = pmcPattern.match(QString::fromStdString(candidate));
        if (pmcMatch.hasMatch()) {
            const QString pmcid = pmcMatch.captured(1).toUpper();
            appendCandidate(QStringLiteral(
                "https://www.ncbi.nlm.nih.gov/research/bionlp/RESTful/pmcoa.cgi/"
                "BioC_json/%1/unicode").arg(pmcid).toStdString());
            appendCandidate(QStringLiteral(
                "https://www.ebi.ac.uk/europepmc/webservices/rest/%1/fullTextXML")
                                .arg(pmcid).toStdString());
            appendCandidate(QStringLiteral(
                "https://europepmc.org/articles/%1?pdf=render").arg(pmcid).toStdString());
        }
        const auto pubmedMatch = pubmedPattern.match(QString::fromStdString(candidate));
        if (!pubmedMatch.hasMatch())
            continue;
        const QString pmid = pubmedMatch.captured(1);
        const QString pmcid = resolvePmcidViaElink(pmid);
        if (!pmcid.isEmpty()) {
            appendCandidate(QStringLiteral(
                "https://www.ncbi.nlm.nih.gov/research/bionlp/RESTful/pmcoa.cgi/"
                "BioC_json/%1/unicode").arg(pmcid).toStdString());
            appendCandidate(QStringLiteral(
                "https://www.ebi.ac.uk/europepmc/webservices/rest/%1/fullTextXML")
                                .arg(pmcid).toStdString());
            appendCandidate(QStringLiteral(
                "https://europepmc.org/articles/%1?pdf=render").arg(pmcid).toStdString());
        }
        appendCandidate(QStringLiteral(
            "https://www.ebi.ac.uk/europepmc/webservices/rest/%1/fullTextXML")
                            .arg(pmid).toStdString());
    }
    std::string errors;
    std::string structuredText;
    std::string structuredKind; // "json"（NCBI BioC）或 "xml"（Europe PMC fullTextXML）
    for (const std::string &candidate : candidates) {
        if (candidate.rfind("http://", 0) == 0 || candidate.rfind("https://", 0) == 0) {
            const auto fetched = fetchUrl(candidate);
            outcome.downloadAttempts.insert(outcome.downloadAttempts.end(),
                                            fetched.attempts.begin(), fetched.attempts.end());
            if (!fetched.body) {
                if (!errors.empty()) errors += "; ";
                const std::string error = fetched.attempts.empty()
                                              ? "request failed"
                                              : fetched.attempts.back().error;
                errors += candidate + ": " + error;
                continue;
            }
            raw = *fetched.body;
            if (candidate.find("/pmcoa.cgi/BioC_json/") != std::string::npos) {
                structuredText = extractBioCText(raw);
                if (structuredText.empty()) {
                    if (!errors.empty()) errors += "; ";
                    errors += candidate + ": BioC full text unavailable";
                    if (!outcome.downloadAttempts.empty()) {
                        auto &attempt = outcome.downloadAttempts.back();
                        attempt.success = false;
                        attempt.error = "BioC full text unavailable";
                    }
                    raw.clear();
                    continue;
                }
                structuredKind = "json";
            } else if (candidate.find("/europepmc/webservices/rest/") != std::string::npos
                       && candidate.ends_with("/fullTextXML")) {
                structuredText = extractEuropePmcXmlText(raw);
                if (structuredText.empty()) {
                    if (!errors.empty()) errors += "; ";
                    errors += candidate + ": Europe PMC full text unavailable";
                    if (!outcome.downloadAttempts.empty()) {
                        auto &attempt = outcome.downloadAttempts.back();
                        attempt.success = false;
                        attempt.error = "Europe PMC full text unavailable";
                    }
                    raw.clear();
                    continue;
                }
                structuredKind = "xml";
            }
            if (item.kind == "paper") {
                const bool pdf = raw.size() >= 5 && raw.compare(0, 5, "%PDF-") == 0;
                const bool html = raw.find("<html") != std::string::npos
                                  || raw.find("<!DOCTYPE") != std::string::npos;
                if (!pdf && html) {
                    if (!errors.empty()) errors += "; ";
                    errors += candidate + ": returned HTML instead of PDF";
                    if (!outcome.downloadAttempts.empty()) {
                        auto &attempt = outcome.downloadAttempts.back();
                        attempt.success = false;
                        attempt.retryable = false;
                        attempt.error = "returned HTML instead of PDF";
                    }
                    raw.clear();
                    continue;
                }
            }
            outcome.resolvedSource = candidate;
            break;
        } else {
            QFile file(QString::fromStdString(candidate));
            DownloadAttempt attempt;
            attempt.source = candidate;
            if (!file.open(QIODevice::ReadOnly)) {
                attempt.error = "file open failed";
                outcome.downloadAttempts.push_back(attempt);
                if (!errors.empty()) errors += "; ";
                errors += "file open failed: " + candidate;
                continue;
            }
            const QByteArray bytes = file.readAll();
            raw = std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
            if (candidate.ends_with(".bioc.json")) {
                structuredText = extractBioCText(raw);
                if (structuredText.empty()) {
                    attempt.error = "BioC full text unavailable";
                    outcome.downloadAttempts.push_back(attempt);
                    raw.clear();
                    if (!errors.empty()) errors += "; ";
                    errors += candidate + ": BioC full text unavailable";
                    continue;
                }
                structuredKind = "json";
            } else if (candidate.ends_with(".fulltext.xml")) {
                structuredText = extractEuropePmcXmlText(raw);
                if (structuredText.empty()) {
                    attempt.error = "Europe PMC full text unavailable";
                    outcome.downloadAttempts.push_back(attempt);
                    raw.clear();
                    if (!errors.empty()) errors += "; ";
                    errors += candidate + ": Europe PMC full text unavailable";
                    continue;
                }
                structuredKind = "xml";
            }
            attempt.success = !raw.empty();
            if (!attempt.success) {
                attempt.error = "empty file";
                if (!errors.empty()) errors += "; ";
                errors += candidate + ": empty file";
            }
            outcome.downloadAttempts.push_back(attempt);
            outcome.resolvedSource = candidate;
            if (!raw.empty())
                break;
        }
    }
    if (raw.empty()) {
        outcome.status = "failed";
        outcome.message = errors.empty() ? "no source candidates" : errors;
        return outcome;
    }

    // 2) 指纹与去重（同一内容不重复入库；已归档条目不阻断重新导入）
    outcome.sha256 = sha256Of(raw);
    bool duplicateContent = false;
    QString duplicateTitle;
    {
        QSqlQuery duplicate(m_database);
        duplicate.prepare(QStringLiteral(
            "SELECT i.title FROM knowledge_versions_v5 v "
            "JOIN knowledge_items_v5 i ON i.id=v.knowledge_item_id "
            "WHERE v.content_hash=? AND i.status<>? ORDER BY v.id LIMIT 1"));
        duplicate.addBindValue(QString::fromStdString(outcome.sha256));
        duplicate.addBindValue(QStringLiteral("archived"));
        duplicateContent = duplicate.exec() && duplicate.next();
        if (duplicateContent)
            duplicateTitle = duplicate.value(0).toString();
    }

    // 3) 文本提取（PDF 用最小提取器；网页剥标签；纯文本直读）
    std::string text;
    std::string extractionTool = "plain-text";
    std::string extractionVersion = "1";
    const bool looksLikePdf = raw.size() >= 5 && raw.compare(0, 5, "%PDF-") == 0;
    if (!structuredText.empty()) {
        text = structuredText;
        extractionTool = structuredKind == "xml" ? "europe-pmc-fulltext-xml"
                                                 : "ncbi-bioc-json";
        extractionVersion = "1";
    } else if (item.kind == "paper" && looksLikePdf) {
        std::string error;
        const auto extracted = ReliablePdfTextExtractor::extract(raw, &error);
        if (!extracted) {
            outcome.status = "failed";
            outcome.message = "pdf extraction failed: " + error;
            return outcome;
        }
        text = extracted->text;
        extractionTool = extracted->extractor;
        extractionVersion = extracted->extractorVersion;
    } else if (raw.rfind("<!DOCTYPE", 0) == 0 || raw.rfind("<html", 0) == 0
               || raw.find("<html") != std::string::npos) {
        text = stripHtml(raw);
    } else {
        text = raw;
    }
    if (text.empty()) {
        outcome.status = "failed";
        outcome.message = "empty extracted text";
        return outcome;
    }

    // 4) 原始 PDF 进入本地受管资产；先原子落盘，再按 SHA-256 登记并晋升。
    if (looksLikePdf || !structuredText.empty()) {
        const QString databasePath = QFileInfo(m_database.databaseName()).absolutePath();
        const QString extension = looksLikePdf
                                      ? QStringLiteral("pdf")
                                      : structuredKind == "xml" ? QStringLiteral("xml")
                                                                : QStringLiteral("json");
        const QString relativePath = QStringLiteral("assets/%1.%2")
                                         .arg(QString::fromStdString(outcome.sha256), extension);
        const QString absolutePath = QDir(databasePath).filePath(relativePath);
        if (!QDir().mkpath(QFileInfo(absolutePath).absolutePath())) {
            outcome.status = "failed";
            outcome.message = "managed asset directory creation failed";
            return outcome;
        }
        if (!QFileInfo::exists(absolutePath)) {
            QSaveFile saved(absolutePath);
            if (!saved.open(QIODevice::WriteOnly)
                || saved.write(raw.data(), static_cast<qint64>(raw.size()))
                       != static_cast<qint64>(raw.size())
                || !saved.commit()) {
                outcome.status = "failed";
                outcome.message = "managed PDF write failed";
                return outcome;
            }
        }
        SqlFileAssetRepository assetRepo(m_database, m_clock);
        Domain::FileAsset asset;
        asset.uid = m_uids.next();
        asset.relativePath = relativePath.toStdString();
        asset.mimeType = looksLikePdf
                             ? "application/pdf"
                             : structuredKind == "xml" ? "application/xml"
                                                       : "application/json";
        asset.byteSize = static_cast<qint64>(raw.size());
        asset.sha256 = outcome.sha256;
        asset.originalName = inferTitle(outcome.resolvedSource) + "." + extension.toStdString();
        asset.storageState = Domain::FileStorageState::Quarantine;
        const auto registered = assetRepo.registerAsset(asset);
        if (!registered) {
            outcome.status = "failed";
            outcome.message = "managed PDF registration failed: " + registered.error().message;
            return outcome;
        }
        Domain::FileAsset managed = registered.value();
        if (managed.storageState == Domain::FileStorageState::Quarantine) {
            const auto promoted = assetRepo.promote(managed.uid, managed.revision);
            if (!promoted) {
                outcome.status = "failed";
                outcome.message = "managed PDF promotion failed: " + promoted.error().message;
                return outcome;
            }
            managed = promoted.value();
        }
        outcome.assetUid = managed.uid.value();
        outcome.assetRelativePath = managed.relativePath;
    }

    // 已有知识条目不重复导入，但仍允许旧数据在本次运行补齐受管原件。
    if (duplicateContent) {
        outcome.status = "duplicate";
        if (!duplicateTitle.isEmpty())
            outcome.message = QStringLiteral("该论文已存在：%1（内容指纹重复，"
                                             "可在搜索框按标题找到）")
                                 .arg(duplicateTitle).toStdString();
        return outcome;
    }

    // 5) 导入（与产品相同的 KnowledgeUseCases 正式结构 + FTS 索引）
    SqlKnowledgeRepository knowledgeRepo(m_database, m_clock);
    SqlKnowledgeFtsIndex fts(m_database, m_clock);
    Application::KnowledgeUseCases useCases(knowledgeRepo, fts, m_uids, m_clock, m_embeddings,
                                            m_vectorStore);

    Application::KnowledgeUseCases::ImportInput input;
    input.libraryType = Domain::LibraryType::Paper;
    input.title = item.title.empty() ? inferTitle(outcome.resolvedSource) : item.title;
    input.domainCode = item.domainCode.empty() ? "unclassified" : item.domainCode;
    input.referenceCode = item.paperNumber;   // 索引来源的论文编号(如 7.41)
    input.summary = text.size() > 2000 ? text.substr(0, 2000) : text;
    input.contentHash = outcome.sha256;
    input.createdBy = "developer";
    input.extractionModel = extractionTool;
    input.extractionPromptVersion = extractionVersion;
    Domain::SourceRecord source;
    source.sourceType = outcome.resolvedSource.rfind("http", 0) == 0 ? "url" : "file";
    source.canonicalUri = outcome.resolvedSource;
    source.title = input.title;
    if (!item.publishedYear.empty())
        source.publishedAt = item.publishedYear;
    QJsonObject metadata;
    metadata.insert(QStringLiteral("paper_number"), QString::fromStdString(item.paperNumber));
    metadata.insert(QStringLiteral("index_source"), QString::fromStdString(item.indexSourceFile));
    metadata.insert(QStringLiteral("index_start_line"), item.indexStartLine);
    metadata.insert(QStringLiteral("index_end_line"), item.indexEndLine);
    QJsonArray focus;
    for (const auto &value : item.researchFocus)
        focus.append(QString::fromStdString(value));
    metadata.insert(QStringLiteral("research_focus"), focus);
    source.metadataJson = QJsonDocument(metadata).toJson(QJsonDocument::Compact).toStdString();
    source.contentHash = outcome.sha256;
    source.trustTier = "unverified";
    input.source = source;
    Domain::PaperDetail paper;
    paper.studyType = item.studyType.empty() ? "unspecified" : item.studyType;
    QJsonObject publication;
    publication.insert(QStringLiteral("year"), QString::fromStdString(item.publishedYear));
    publication.insert(QStringLiteral("paper_number"), QString::fromStdString(item.paperNumber));
    paper.publicationJson =
        QJsonDocument(publication).toJson(QJsonDocument::Compact).toStdString();
    input.paper = paper;

    const auto imported = useCases.importKnowledge(input);
    if (!imported) {
        outcome.status = "failed";
        outcome.message = imported.error().message + ": " + imported.error().detail;
        return outcome;
    }
    outcome.ok = true;
    outcome.status = "imported";
    outcome.itemUid = imported.value().item.uid.value();
    outcome.sourceUid = imported.value().sourceUid.value_or("");
    outcome.extractionTool = extractionTool;
    outcome.extractedText = std::move(text);
    return outcome;
}

} // namespace PersonOS::Infrastructure
