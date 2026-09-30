#include "presentation/viewmodels/KnowledgeViewModel.h"

#include <QFileInfo>
#include <QCryptographicHash>
#include <QSqlDatabase>
#include <QUrl>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrent>

#include "application/usecases/knowledge/KnowledgeUseCases.h"
#include "application/usecases/knowledge/PaperAnalysisUseCases.h"
#include "database/DatabaseManager.h"
#include "domain/foundation/Uid.h"
#include "infrastructure/ai/AiGateway.h"
#include "infrastructure/ai/OpenAiCompatibleProvider.h"
#include "infrastructure/ai/SqlAiRepository.h"
#include "infrastructure/ai/WindowsCredentialStore.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/CorpusPipeline.h"
#include "infrastructure/knowledge/ManagedTextExtraction.h"
#include "infrastructure/knowledge/SqlEmbeddingRepository.h"
#include "infrastructure/knowledge/SqlKnowledgeFtsIndex.h"
#include "infrastructure/embedding/DomainClassifier.h"
#include "infrastructure/embedding/LocalEmbeddingProvider.h"
#include "infrastructure/persistence/DatabaseConnectionFactory.h"
#include "infrastructure/persistence/SqlFileAssetRepository.h"
#include "infrastructure/persistence/SqlKnowledgeRepository.h"
#include "presentation/viewmodels/VmSupport.h"

namespace PersonOS {

namespace {

QVariantMap row(const Domain::KnowledgeItem &item)
{
    const QString type = QString::fromStdString(Domain::toString(item.libraryType));
    const QString subtitle =
        item.referenceCode.empty()
            ? Presentation::knowledgeDomainLabel(QString::fromStdString(item.domainCode))
            : QStringLiteral("%1 · %2")
                  .arg(QString::fromStdString(item.referenceCode),
                       Presentation::knowledgeDomainLabel(
                           QString::fromStdString(item.domainCode)));
    return {{QStringLiteral("uid"), QString::fromStdString(item.uid.value())},
            {QStringLiteral("title"), QString::fromStdString(item.title)},
            {QStringLiteral("subtitle"), subtitle},
            // badge 展示中文标签;kind 保留原始类型键(分类过滤用)
            {QStringLiteral("badge"), Presentation::knowledgeTypeLabel(type)},
            {QStringLiteral("kind"), type},
            {QStringLiteral("badgeTone"),
             item.status == Domain::KnowledgeStatus::Warned ? QStringLiteral("warning")
                                                            : QStringLiteral("info")},
            {QStringLiteral("detail"),
             Presentation::knowledgeStatusLabel(
                 QString::fromStdString(Domain::toString(item.status)))},
            {QStringLiteral("retractable"),
             item.status == Domain::KnowledgeStatus::Candidate
                 && item.createdBy == "generated"}};
}

} // namespace

KnowledgeViewModel::KnowledgeViewModel(QObject *parent) : QObject(parent) {}

void KnowledgeViewModel::setState(const QString &state)
{
    if (m_pageState == state)
        return;
    m_pageState = state;
    emit pageStateChanged();
}

void KnowledgeViewModel::setError(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
}

void KnowledgeViewModel::setNotice(const QString &message)
{
    m_notice = message;
    emit noticeChanged();
}

namespace {
// 结构化 JSON 数组 → 可读行:优先取对象的 text/name 字段,否则紧凑 JSON 截断
QStringList readableJsonLines(const QString &json)
{
    QStringList out;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    const auto compact = [](const QJsonValue &value) {
        QJsonDocument compactDoc;
        if (value.isArray())
            compactDoc = QJsonDocument(value.toArray());
        else if (value.isObject())
            compactDoc = QJsonDocument(value.toObject());
        else if (value.isString())
            return value.toString().left(240);
        else
            return QString();
        return QString::fromUtf8(compactDoc.toJson(QJsonDocument::Compact)).left(240);
    };
    if (doc.isArray()) {
        for (const auto &value : doc.array()) {
            if (value.isObject()) {
                const QJsonObject object = value.toObject();
                const QString text =
                    object.value(QStringLiteral("text")).toString();
                if (!text.trimmed().isEmpty()) {
                    out.append(text);
                    continue;
                }
            }
            out.append(compact(value));
        }
    } else if (doc.isObject()) {
        out.append(compact(QJsonValue(doc.object())));
    }
    return out;
}
} // namespace

void KnowledgeViewModel::showItems(const std::vector<Domain::KnowledgeItem> &items)
{
    m_allRows.clear();
    for (const auto &item : items)
        m_allRows.append(row(item));
    applyFilter();
}

void KnowledgeViewModel::setKind(const QString &kind)
{
    if (m_currentKind == kind)
        return;
    m_currentKind = kind;
    emit kindChanged();
    applyFilter();
}

void KnowledgeViewModel::requestDomainSuggestions(const QString &filePath)
{
    m_domainSuggestions.clear();
    emit domainSuggestionsChanged();
    const QString title = QFileInfo(filePath).completeBaseName().trimmed();
    if (title.isEmpty())
        return;
    // 后台线程:首次调用会加载本地模型(约 1-2 秒),零 API 成本
    QtConcurrent::run([title]() {
        const auto suggestions = Infrastructure::DomainClassifier::instance().suggest(title, 3);
        QStringList labels;
        for (const auto &suggestion : suggestions)
            labels.append(QStringLiteral("%1 %2").arg(suggestion.code, suggestion.title));
        return labels;
    }).then([this](const QStringList &labels) {
        m_domainSuggestions = labels;
        emit domainSuggestionsChanged();
    });
}

void KnowledgeViewModel::applyFilter()
{
    QVariantList rows;
    for (const auto &value : m_allRows) {
        const QVariantMap map = value.toMap();
        if (m_currentKind.isEmpty()
            || map.value(QStringLiteral("kind")).toString() == m_currentKind)
            rows.append(map);
    }
    m_itemsModel.replace(rows);
    setState(rows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"));
    emit dataChanged();
}

void KnowledgeViewModel::refresh()
{
    setState(QStringLiteral("loading"));
    setError({});
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlKnowledgeRepository repo(database, clock);
    showItems(repo.listRecent(100));
}

void KnowledgeViewModel::search(const QString &text)
{
    setState(QStringLiteral("loading"));
    setError({});
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlKnowledgeRepository repo(database, clock);
    if (text.trimmed().isEmpty()) {
        showItems(repo.listRecent(100));
        return;
    }
    showItems(repo.findItemsByTitle(text.trimmed().toStdString()));
}

void KnowledgeViewModel::addItem(const QString &kind, const QString &title,
                                 const QString &summary)
{
    const auto type = Domain::libraryTypeFrom(kind.toStdString());
    if (!type || title.trimmed().isEmpty() || summary.trimmed().isEmpty()) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("请选择类型并填写标题与摘要"));
        return;
    }
    if (*type == Domain::LibraryType::Paper) {
        // 论文走单文献处理核心（PDF/文本摄入），不在本表单直接建条目
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("论文请通过导入文件添加（经单文献处理核心）"));
        return;
    }

    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlKnowledgeRepository repo(database, clock);
    Infrastructure::SqlKnowledgeFtsIndex index(database, clock);
    Infrastructure::SqlEmbeddingRepository embeddings(database);
    Application::KnowledgeUseCases useCases(repo, index, uids, clock,
                                            &Infrastructure::sharedLocalEmbedding(),
                                            &embeddings);

    Application::KnowledgeUseCases::ImportInput input;
    input.libraryType = *type;
    input.title = title.trimmed().toStdString();
    input.domainCode = "learning";
    input.summary = summary.trimmed().toStdString();
    input.contentHash = std::string("manual:") + uids.next().value();
    input.createdBy = "user";
    const auto imported = useCases.importKnowledge(input);
    if (!imported) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(imported.error().message,
                                             imported.error().detail));
        return;
    }
    refresh();
}

void KnowledgeViewModel::importPaperFile(const QString &fileUrl, const QString &domainSlug)
{
    const QString path = QUrl(fileUrl).toLocalFile();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("请选择有效的 PDF 或文本文件"));
        return;
    }
    setState(QStringLiteral("loading"));
    setError({});
    // 提取工具(Poppler/Tesseract)路径:设置页配置优先,其次常见位置探测
    Presentation::applyPdfToolPaths();
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    // 领域与编号(基于 Research Base 01-43):research.NN.slug + NN.<序号>
    QString domainCode = QStringLiteral("unclassified");
    std::string referenceCode;
    if (!domainSlug.trimmed().isEmpty()) {
        Infrastructure::SqlKnowledgeRepository repo(database, clock);
        domainCode = QStringLiteral("research.") + domainSlug.trimmed().toLower();
        const int existing = repo.countPapersInDomain(domainCode.toStdString());
        const int number = domainSlug.trimmed().left(2).toInt();
        referenceCode = QStringLiteral("%1.%2")
                            .arg(number, 2, 10, QLatin1Char('0'))
                            .arg(existing + 1).toStdString();
    }
    Infrastructure::SqlEmbeddingRepository embeddings(database);
    Infrastructure::CorpusPipeline pipeline(database, uids, clock,
                                            &Infrastructure::sharedLocalEmbedding(),
                                            &embeddings);
    Infrastructure::CorpusPipeline::Item item;
    item.source = path.toStdString();
    item.title = QFileInfo(path).completeBaseName().toStdString();
    item.kind = "paper";
    item.domainCode = domainCode.toStdString();
    item.paperNumber = referenceCode;
    const auto outcome = pipeline.process(item);
    if (outcome.status == QStringLiteral("duplicate")) {
        setState(QStringLiteral("conflict"));
        const QString message = QString::fromStdString(outcome.message);
        setError(message.isEmpty()
                     ? QStringLiteral("该论文已存在（内容指纹重复），无需重复导入")
                     : message.left(240));
        return;
    }
    if (outcome.status != QStringLiteral("imported")) {
        setState(QStringLiteral("error"));
        const QString message = QString::fromStdString(outcome.message);
        setError(message.isEmpty() ? QStringLiteral("论文导入失败")
                                   : message.left(240));
        return;
    }
    refresh();
}

void KnowledgeViewModel::convertPaperToMethods(const QString &itemUidText)
{
    const auto parsed = Domain::Uid::parse(itemUidText.toStdString());
    if (!parsed) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("无效的知识条目"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlKnowledgeRepository repo(database, clock);
    const auto item = repo.findItem(*parsed);
    if (!item || item->libraryType != Domain::LibraryType::Paper) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("请选择论文条目进行转化"));
        return;
    }
    Infrastructure::SqlAiRepository aiRepo(database, clock);
    const auto config = aiRepo.findFirstEnabledConfig();
    if (!config) {
        setState(QStringLiteral("offline"));
        setError(QStringLiteral("未配置可用的模型连接：请到设置页添加、测试并启用连接"));
        return;
    }

    // 后台线程执行:受管原件全文提取 → AI 分析 → 候选方法/贴士入库
    setState(QStringLiteral("ai_waiting"));
    setError({});
    setNotice({});
    const QString dbPath = DatabaseManager::instance().databasePath();
    const std::string paperUid = parsed->value();
    const std::string modelVersion = config->providerCode + ":" + config->model;
    const std::string configUid = config->uid.value();
    QtConcurrent::run([this, dbPath, paperUid, configUid, modelVersion]() {
        Infrastructure::DatabaseConnectionFactory factory(dbPath);
        QString openError;
        QSqlDatabase workerDb =
            factory.openForCurrentThread(QStringLiteral("ai_convert_paper"), &openError);
        if (!workerDb.isOpen())
            return QStringLiteral("数据库连接失败：") + openError;
        const auto fail = [&](const QString &message) {
            Infrastructure::DatabaseConnectionFactory::closeCurrentThreadConnection(
                QStringLiteral("ai_convert_paper"));
            return message;
        };

        Infrastructure::QtSystemClock clock;
        Infrastructure::QtUidGenerator uids;
        Infrastructure::SqlKnowledgeRepository repo(workerDb, clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(workerDb, clock);
        Infrastructure::SqlEmbeddingRepository embeddings(workerDb);
        Application::KnowledgeUseCases knowledge(repo, fts, uids, clock,
                                                 &Infrastructure::sharedLocalEmbedding(),
                                                 &embeddings);
        Infrastructure::SqlAiRepository aiRepo(workerDb, clock);
        Infrastructure::WindowsCredentialStore credentials;
        Infrastructure::OpenAiCompatibleProvider provider(credentials);
        Infrastructure::AiGateway gateway(aiRepo, provider, uids, clock);
        Application::PaperAnalysisUseCases analyzer(gateway, repo, knowledge, uids, clock);

        const auto paper = repo.findItem(*Domain::Uid::parse(paperUid));
        if (!paper)
            return fail(QStringLiteral("论文不存在"));
        // 原始导入版本的 content_hash = 原件指纹(ai 草稿版本不带原件指纹)
        std::string contentHash;
        for (const auto &version : repo.versionsOf(paper->uid))
            if (version.createdBy != "ai" && !version.contentHash.empty()) {
                contentHash = version.contentHash;
                break;
            }
        if (contentHash.empty())
            return fail(QStringLiteral("论文缺少内容指纹，无法定位原件"));
        const auto source = repo.findSourceByContentHash(contentHash);
        if (!source)
            return fail(QStringLiteral("论文来源记录缺失（按内容指纹未找到）"));
        Infrastructure::SqlFileAssetRepository assetRepo(workerDb, clock);
        const auto asset = assetRepo.findBySha256(contentHash);
        if (!asset)
            return fail(QStringLiteral("论文原件缺失（受管资产未找到）"));
        const QString assetPath = QFileInfo(dbPath).absolutePath() + QStringLiteral("/")
                                  + QString::fromStdString(asset->relativePath);
        Presentation::applyPdfToolPaths();   // PDF 提取需要 pdftotext/OCR 工具
        std::string extractError;
        const auto text = Infrastructure::extractManagedFullText(assetPath, extractError);
        if (!text || text->empty())
            return fail(QStringLiteral("论文全文提取失败：")
                        + QString::fromStdString(extractError));

        Application::PaperAnalysisUseCases::AnalyzeInput input;
        input.paperItemUid = paper->uid;
        input.sourceUid = source->uid;
        input.sourceAssetUid = asset->uid;
        input.providerConfigUid = *Domain::Uid::parse(configUid);
        input.title = paper->title;
        input.extractedText = *text;
        input.modelVersion = modelVersion;
        // 幂等键与分析指纹纳入条目身份:重新导入同内容论文时任务键不复用,
        // 旧条目的历史任务键成为无害孤儿(此前曾因此报
        // "duplicate job idempotency key")
        input.idempotencyKey = "paper-analysis:" + paper->uid.value() + ":"
                               + contentHash + ":" + modelVersion + ":v2";
        input.analysisContentHash = QCryptographicHash::hash(
            QByteArray::fromStdString(paper->uid.value() + contentHash + modelVersion
                                      + ":v2"),
            QCryptographicHash::Sha256).toHex().toStdString();
        const auto result = analyzer.analyze(input);
        if (!result) {
            qWarning().noquote() << "[convert] analyze failed for" << paper->uid.value().c_str()
                                 << ":" << QString::fromStdString(result.error().message)
                                 << "|" << QString::fromStdString(result.error().detail);
            return fail(Presentation::friendlyError(result.error().message,
                                                    result.error().detail));
        }
        const QString message =
            QStringLiteral("转化完成：生成 %1 个候选方法、%2 个候选贴士、%3 条证据片段，"
                           "已写入方法/贴士库候选区（带证据与来源，可撤销）")
                .arg(result.value().candidateMethods.size())
                .arg(result.value().candidateTips.size())
                .arg(result.value().evidenceFragmentCount);
        Infrastructure::DatabaseConnectionFactory::closeCurrentThreadConnection(
            QStringLiteral("ai_convert_paper"));
        return QStringLiteral("OK:") + message;
    }).then([this](const QString &resultText) {
        if (resultText.startsWith(QStringLiteral("OK:"))) {
            setNotice(resultText.mid(3));
            refresh();
        } else {
            setState(QStringLiteral("error"));
            setError(resultText);
        }
    });
}

void KnowledgeViewModel::openItem(const QString &itemUidText)
{
    const auto parsed = Domain::Uid::parse(itemUidText.toStdString());
    if (!parsed) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("无效的知识条目"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlKnowledgeRepository repo(database, clock);
    const auto item = repo.findItem(*parsed);
    if (!item) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("条目不存在"));
        return;
    }
    m_detailTitle = QString::fromStdString(item->title);
    m_detailMeta = QStringLiteral("%1 · 编号 %2 · %3 · 状态 %4")
                       .arg(QString::fromStdString(Domain::toString(item->libraryType)),
                            QString::fromStdString(item->referenceCode.empty()
                                                       ? std::string("—")
                                                       : item->referenceCode),
                            QString::fromStdString(item->domainCode),
                            QString::fromStdString(Domain::toString(item->status)));
    m_detailSummary.clear();
    m_detailClaims.clear();
    m_detailApplicability.clear();
    m_detailLimitations.clear();
    m_detailSteps.clear();
    m_detailEvidence.clear();

    const auto versions = repo.versionsOf(item->uid);
    Domain::KnowledgeVersion current;
    bool foundCurrent = false;
    for (const auto &version : versions)
        if (item->currentVersionUid && version.uid.value() == *item->currentVersionUid) {
            current = version;
            foundCurrent = true;
            break;
        }
    if (foundCurrent) {
        m_detailSummary = QString::fromStdString(current.summary);
        m_detailClaims = readableJsonLines(QString::fromStdString(current.claimsJson));
        m_detailApplicability =
            readableJsonLines(QString::fromStdString(current.applicabilityJson));
        m_detailLimitations =
            readableJsonLines(QString::fromStdString(current.limitationsJson));
        // 方法步骤与证据片段均按当前版本关联
        int sequence = 1;
        for (const auto &step : repo.methodStepsOf(current.uid))
            m_detailSteps.append(QStringLiteral("%1. %2")
                                     .arg(sequence++).arg(QString::fromStdString(
                                         step.instruction)));
        for (const auto &fragment : repo.evidenceFragmentsOf(current.uid))
            m_detailEvidence.append(QString::fromStdString(fragment.text));
    }
    if (item->libraryType == Domain::LibraryType::Method)
        if (const auto detail = repo.methodDetailOf(item->uid))
            m_detailMeta += QStringLiteral(" · 风险 %1 · 证据 %2")
                                .arg(QString::fromStdString(detail->riskLevel),
                                     QString::fromStdString(detail->evidenceGrade));

    m_detailVisible = true;
    emit detailChanged();
}

void KnowledgeViewModel::closeDetail()
{
    m_detailVisible = false;
    emit detailChanged();
}

void KnowledgeViewModel::removeGeneratedItem(const QString &itemUidText)
{
    const auto parsed = Domain::Uid::parse(itemUidText.toStdString());
    if (!parsed) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("无效的知识条目"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlKnowledgeRepository repo(database, clock);
    const auto item = repo.findItem(*parsed);
    if (!item || item->status != Domain::KnowledgeStatus::Candidate
        || item->createdBy != "generated") {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("仅 AI 生成的候选条目可删除（论文、用户条目与历史版本不可删除）"));
        return;
    }
    // 经 derived_from 找到来源论文
    std::optional<Domain::Uid> paperUid;
    for (const auto &relation : repo.relationsOf(item->uid))
        if (relation.relation == "derived_from"
            && relation.fromItemUid == item->uid.value()) {
            paperUid = Domain::Uid::parse(relation.toItemUid);
            break;
        }
    if (!paperUid) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("未找到来源论文关系，无法删除"));
        return;
    }

    Infrastructure::SqlKnowledgeFtsIndex fts(database, clock);
    Infrastructure::SqlEmbeddingRepository embeddings(database);
    Application::KnowledgeUseCases knowledge(repo, fts, uids, clock,
                                             &Infrastructure::sharedLocalEmbedding(),
                                             &embeddings);
    Infrastructure::SqlAiRepository aiRepo(database, clock);
    Infrastructure::WindowsCredentialStore credentials;
    Infrastructure::OpenAiCompatibleProvider provider(credentials);
    Infrastructure::AiGateway gateway(aiRepo, provider, uids, clock);
    Application::PaperAnalysisUseCases useCases(gateway, repo, knowledge, uids, clock);
    Application::PaperAnalysisUseCases::PurgeInput input;
    input.paperItemUid = *paperUid;
    input.candidateItemUids.push_back(item->uid);
    const auto purged = useCases.purgeGeneratedCandidates(input);
    if (!purged) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(purged.error().message,
                                             purged.error().detail));
        return;
    }
    setNotice(QStringLiteral("已彻底删除 %1 个候选条目（级联清除版本/步骤/关系与检索索引）")
                  .arg(purged.value().purgedCount));
    refresh();
}

} // namespace PersonOS
