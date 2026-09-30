// 开发者文献批量脚手架（DR-009；scope-v1 §5 工程交付物，不进入普通用户 UI）
// 用法：
//   corpus_scaffold --db <工作库路径> --list <清单.json> [--report <报告路径>]
//   corpus_scaffold --db <工作库路径> --dir <目录> --pattern <*.pdf|*.txt>
//   corpus_scaffold --db <工作库路径> --export-package <包文件路径>
// 清单格式：[{"source":"url 或本地路径","title":"可选标题","kind":"paper|text"}]
// 断点续作：--report 存在时跳过其中 sha256 已成功的条目；报告含成功/失败/重复。
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QStringList>
#include <QDir>

#include <cstdio>
#include <string>
#include <memory>

#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/CorpusPipeline.h"
#include "infrastructure/knowledge/KnowledgePackage.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/knowledge/MarkdownCorpusIndex.h"
#include "infrastructure/knowledge/SqlKnowledgeFtsIndex.h"
#include "infrastructure/persistence/SqlKnowledgeRepository.h"
#include "application/usecases/knowledge/KnowledgeUseCases.h"
#include "application/usecases/knowledge/PaperAnalysisUseCases.h"
#include "infrastructure/ai/AiGateway.h"
#include "infrastructure/ai/OpenAiCompatibleProvider.h"
#include "infrastructure/ai/SqlAiRepository.h"
#include "infrastructure/ai/WindowsCredentialStore.h"

using namespace PersonOS;

namespace {

struct Options
{
    QString dbPath;
    QString listPath;
    QString dirPath;
    QString pattern = QStringLiteral("*.txt");
    QString reportPath;
    QString exportPackagePath;
    QString indexDirPath;
    bool analyze = false;
    int sampleSize = 0;
    int targetSuccesses = 0;
    QString pdfToTextPath;
    QString pdfToPpmPath;
    QString tesseractPath;
};

Options parseOptions(const QStringList &arguments)
{
    Options options;
    for (int i = 1; i < arguments.size(); ++i) {
        const QString argument = arguments[i];
        const auto next = [&]() { return i + 1 < arguments.size() ? arguments[++i]
                                                                  : QString(); };
        if (argument == QStringLiteral("--db"))
            options.dbPath = next();
        else if (argument == QStringLiteral("--list"))
            options.listPath = next();
        else if (argument == QStringLiteral("--dir"))
            options.dirPath = next();
        else if (argument == QStringLiteral("--pattern"))
            options.pattern = next();
        else if (argument == QStringLiteral("--report"))
            options.reportPath = next();
        else if (argument == QStringLiteral("--export-package"))
            options.exportPackagePath = next();
        else if (argument == QStringLiteral("--index-dir"))
            options.indexDirPath = next();
        else if (argument == QStringLiteral("--analyze"))
            options.analyze = true;
        else if (argument == QStringLiteral("--sample-size"))
            options.sampleSize = next().toInt();
        else if (argument == QStringLiteral("--target-successes"))
            options.targetSuccesses = next().toInt();
        else if (argument == QStringLiteral("--pdftotext"))
            options.pdfToTextPath = next();
        else if (argument == QStringLiteral("--pdftoppm"))
            options.pdfToPpmPath = next();
        else if (argument == QStringLiteral("--tesseract"))
            options.tesseractPath = next();
    }
    return options;
}

QJsonObject loadReport(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool saveReport(const QString &path, const QJsonObject &report)
{
    if (path.isEmpty())
        return true;
    const QByteArray bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(bytes) != bytes.size() || !file.commit())
        return false;
    const QByteArray digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    QSaveFile checksum(path + QStringLiteral(".sha256"));
    if (!checksum.open(QIODevice::WriteOnly)
        || checksum.write(digest + QByteArrayLiteral("  ")
                          + QFileInfo(path).fileName().toUtf8() + '\n') < 0
        || !checksum.commit())
        return false;
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("PersonalOS"));
    app.setApplicationName(QStringLiteral("PersonalOSCorpusScaffold"));

    const Options options = parseOptions(app.arguments());
    if (options.dbPath.isEmpty()
        || (options.listPath.isEmpty() && options.dirPath.isEmpty()
            && options.indexDirPath.isEmpty()
            && options.exportPackagePath.isEmpty())) {
        std::fprintf(stderr,
                     "用法: corpus_scaffold --db <db> --list <json> | --dir <dir> "
                     "[--pattern *.txt] [--report <json>] | --index-dir <AllPaper> "
                     "[--sample-size 100] [--target-successes 15] [--report <json>] [--analyze] | "
                     "[--pdftotext <path>] [--pdftoppm <path>] [--tesseract <path>] | "
                     "--export-package <file>\n");
        return 1;
    }

    if (!options.pdfToTextPath.isEmpty())
        qputenv("PERSONOS_PDFTOTEXT_PATH", options.pdfToTextPath.toUtf8());
    if (!options.pdfToPpmPath.isEmpty())
        qputenv("PERSONOS_PDFTOPPM_PATH", options.pdfToPpmPath.toUtf8());
    if (!options.tesseractPath.isEmpty())
        qputenv("PERSONOS_TESSERACT_PATH", options.tesseractPath.toUtf8());

    // 工作库（独立于产品数据库；可复用产品库）
    qputenv("PERSONOS_DB_PATH", options.dbPath.toUtf8());
    if (!DatabaseManager::instance().open()) {
        std::fprintf(stderr, "数据库打开失败: %s\n",
                     qPrintable(DatabaseManager::instance().lastError()));
        return 1;
    }
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::LearningManifestSeed(DatabaseManager::instance().database(), clock)
        .ensureSeeded();

    // 知识库安装包导出（与导入共用正式结构）
    if (!options.exportPackagePath.isEmpty()) {
        Infrastructure::KnowledgePackageExporter exporter(
            DatabaseManager::instance().database(), clock);
        const auto json = exporter.exportToJson();
        if (!json) {
            std::fprintf(stderr, "导出失败: %s\n",
                         qPrintable(QString::fromStdString(json.error().message)));
            return 1;
        }
        QFile out(options.exportPackagePath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
            std::fprintf(stderr, "无法写入包文件: %s\n",
                         qPrintable(options.exportPackagePath));
            return 1;
        }
        out.write(QByteArray::fromStdString(json.value()));
        std::printf("已导出知识库包: %s\n", qPrintable(options.exportPackagePath));
        return 0;
    }

    // 条目清单（--list 或 --dir）
    QJsonArray items;
    QVector<Infrastructure::IndexedPaper> indexedPapers;
    QVector<QString> indexedDomainCodes;
    if (!options.listPath.isEmpty()) {
        QFile file(options.listPath);
        if (!file.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "清单读取失败: %s\n", qPrintable(options.listPath));
            return 1;
        }
        items = QJsonDocument::fromJson(file.readAll()).array();
    } else if (!options.dirPath.isEmpty()) {
        QDir dir(options.dirPath);
        const QStringList files =
            dir.entryList(QStringList{options.pattern}, QDir::Files);
        for (const QString &file : files) {
            QJsonObject item;
            item.insert(QStringLiteral("source"), dir.absoluteFilePath(file));
            item.insert(QStringLiteral("kind"), QStringLiteral("paper"));
            items.append(item);
        }
    } else {
        const auto index = Infrastructure::MarkdownCorpusIndex::parseDirectory(
            options.indexDirPath);
        for (const auto &warning : index.warnings)
            std::fprintf(stderr, "索引警告: %s\n", qPrintable(warning));
        QVector<int> positions(index.domains.size(), 0);
        int selected = 0;
        bool added = true;
        while (added && (options.sampleSize <= 0 || selected < options.sampleSize)) {
            added = false;
            for (qsizetype domainIndex = 0; domainIndex < index.domains.size(); ++domainIndex) {
                if (options.sampleSize > 0 && selected >= options.sampleSize)
                    break;
                const auto &domain = index.domains.at(domainIndex);
                const int paperIndex = positions.at(domainIndex);
                if (paperIndex >= domain.papers.size())
                    continue;
                added = true;
                const auto &paper = domain.papers.at(paperIndex);
                positions[domainIndex] = paperIndex + 1;
                const QString domainCode = QStringLiteral("research.%1.%2")
                                               .arg(domain.number, 2, 10, QLatin1Char('0'))
                                               .arg(domain.code);
                indexedPapers.append(paper);
                indexedDomainCodes.append(domainCode);
                ++selected;
            }
        }
    }

    // 断点续作：已成功条目按 sha256 跳过
    QJsonObject report = loadReport(options.reportPath);
    QJsonArray results = report.value(QStringLiteral("results")).toArray();
    QSet<QString> doneSources;
    for (const auto &value : results) {
        const QJsonObject result = value.toObject();
        if (result.value(QStringLiteral("status")).toString()
            == QStringLiteral("imported"))
            doneSources.insert(result.value(QStringLiteral("source")).toString());
    }

    Infrastructure::CorpusPipeline pipeline(DatabaseManager::instance().database(), uids,
                                            clock);
    Infrastructure::SqlAiRepository aiRepo(DatabaseManager::instance().database(), clock);
    const auto analysisConfig = options.analyze ? aiRepo.findFirstEnabledConfig()
                                                : std::nullopt;
    if (options.analyze) {
        if (!analysisConfig) {
            std::fprintf(stderr, "AI 分析拒绝：没有已启用的模型连接\n");
            return 3;
        }
        const auto gate = aiRepo.latestConnectionTest(analysisConfig->uid);
        if (!gate || !gate->overallOk || gate->providerModel != analysisConfig->model) {
            std::fprintf(stderr, "AI 分析拒绝：当前模型尚未通过连接与能力门禁\n");
            return 3;
        }
    }
    Infrastructure::WindowsCredentialStore credentials;
    Infrastructure::OpenAiCompatibleProvider provider(credentials);
    Infrastructure::AiGateway gateway(aiRepo, provider, uids, clock);
    Infrastructure::SqlKnowledgeRepository knowledgeRepo(
        DatabaseManager::instance().database(), clock);
    Infrastructure::SqlKnowledgeFtsIndex analysisFts(
        DatabaseManager::instance().database(), clock);
    Application::KnowledgeUseCases knowledge(knowledgeRepo, analysisFts, uids, clock);
    Application::PaperAnalysisUseCases analyzer(gateway, knowledgeRepo, knowledge, uids, clock);
    int imported = 0;
    int failed = 0;
    int duplicates = 0;
    int analysisFailures = 0;
    int resumedSkipped = 0;
    const qsizetype runResultStart = results.size();
    const qsizetype totalItems = indexedPapers.isEmpty() ? items.size() : indexedPapers.size();
    for (qsizetype itemIndex = 0; itemIndex < totalItems; ++itemIndex) {
        if (options.targetSuccesses > 0 && imported >= options.targetSuccesses)
            break;
        Infrastructure::CorpusPipeline::Item item;
        if (!indexedPapers.isEmpty()) {
            const auto &paper = indexedPapers.at(itemIndex);
            item.title = paper.title.toStdString();
            item.kind = "paper";
            item.domainCode = indexedDomainCodes.at(itemIndex).toStdString();
            item.paperNumber = paper.paperNumber.toStdString();
            item.publishedYear = paper.year.toStdString();
            item.studyType = paper.type.toStdString();
            item.indexSourceFile = paper.sourceFile.toStdString();
            item.indexStartLine = paper.startLine;
            item.indexEndLine = paper.endLine;
            for (const auto &value : paper.researchFocus)
                item.researchFocus.push_back(value.toStdString());
            for (const auto &url : paper.downloadUrls)
                item.sourceCandidates.push_back(url.toStdString());
            for (const auto &url : paper.viewUrls)
                if (!paper.downloadUrls.contains(url))
                    item.sourceCandidates.push_back(url.toStdString());
            if (!item.sourceCandidates.empty())
                item.source = item.sourceCandidates.front();
        } else {
            const QJsonObject object = items.at(itemIndex).toObject();
            item.source = object.value(QStringLiteral("source")).toString().toStdString();
            item.title = object.value(QStringLiteral("title")).toString().toStdString();
            item.kind = object.value(QStringLiteral("kind")).toString("paper").toStdString();
            item.domainCode = object.value(QStringLiteral("domain_code"))
                                  .toString("unclassified").toStdString();
        }
        if (item.source.empty() && item.sourceCandidates.empty()) {
            std::fprintf(stderr, "跳过空 source 条目\n");
            continue;
        }
        if (doneSources.contains(QString::fromStdString(item.source))) {
            std::printf("[resumed-skip] %s\n", item.source.c_str());
            ++resumedSkipped;
            continue;
        }

        // 预先指纹（本地文件）；URL 无法预知内容，直接处理（处理后写入报告）
        const auto outcome = pipeline.process(item);
        QJsonObject result;
        result.insert(QStringLiteral("source"), QString::fromStdString(item.source));
        result.insert(QStringLiteral("status"), QString::fromStdString(outcome.status));
        result.insert(QStringLiteral("message"), QString::fromStdString(outcome.message));
        result.insert(QStringLiteral("sha256"), QString::fromStdString(outcome.sha256));
        result.insert(QStringLiteral("item_uid"), QString::fromStdString(outcome.itemUid));
        result.insert(QStringLiteral("resolved_source"),
                      QString::fromStdString(outcome.resolvedSource));
        result.insert(QStringLiteral("domain_code"), QString::fromStdString(item.domainCode));
        result.insert(QStringLiteral("paper_number"), QString::fromStdString(item.paperNumber));
        result.insert(QStringLiteral("asset_uid"), QString::fromStdString(outcome.assetUid));
        result.insert(QStringLiteral("asset_path"),
                      QString::fromStdString(outcome.assetRelativePath));
        result.insert(QStringLiteral("extraction_tool"),
                      QString::fromStdString(outcome.extractionTool));
        QJsonArray downloadAttempts;
        for (const auto &attempt : outcome.downloadAttempts) {
            QJsonObject attemptJson;
            attemptJson.insert(QStringLiteral("source"),
                               QString::fromStdString(attempt.source));
            attemptJson.insert(QStringLiteral("attempt"), attempt.attempt);
            attemptJson.insert(QStringLiteral("http_status"), attempt.httpStatus);
            attemptJson.insert(QStringLiteral("success"), attempt.success);
            attemptJson.insert(QStringLiteral("retryable"), attempt.retryable);
            attemptJson.insert(QStringLiteral("error"), QString::fromStdString(attempt.error));
            downloadAttempts.append(attemptJson);
        }
        result.insert(QStringLiteral("download_attempts"), downloadAttempts);
        if (options.analyze && outcome.status == "imported") {
            const auto paperUid = Domain::Uid::parse(outcome.itemUid);
            const auto sourceUid = Domain::Uid::parse(outcome.sourceUid);
            if (!paperUid || !sourceUid) {
                result.insert(QStringLiteral("analysis_status"), QStringLiteral("failed"));
                result.insert(QStringLiteral("analysis_message"),
                              QStringLiteral("import outcome missing paper/source uid"));
                ++analysisFailures;
            } else {
                Application::PaperAnalysisUseCases::AnalyzeInput analysis;
                analysis.paperItemUid = *paperUid;
                analysis.sourceUid = *sourceUid;
                if (const auto assetUid = Domain::Uid::parse(outcome.assetUid))
                    analysis.sourceAssetUid = *assetUid;
                analysis.providerConfigUid = analysisConfig->uid;
                analysis.title = item.title;
                analysis.extractedText = outcome.extractedText;
                analysis.modelVersion = analysisConfig->providerCode + ":" + analysisConfig->model;
                analysis.idempotencyKey = "paper-analysis:" + outcome.itemUid + ":"
                                          + outcome.sha256 + ":"
                                          + analysis.modelVersion + ":v2";
                analysis.analysisContentHash = QCryptographicHash::hash(
                    QByteArray::fromStdString(outcome.itemUid + outcome.sha256
                                              + analysis.modelVersion + ":v2"),
                    QCryptographicHash::Sha256).toHex().toStdString();
                const auto analyzed = analyzer.analyze(analysis);
                if (analyzed) {
                    result.insert(QStringLiteral("analysis_status"), QStringLiteral("completed"));
                    result.insert(QStringLiteral("analysis_version_uid"),
                                  QString::fromStdString(analyzed.value().paperVersion.uid.value()));
                    result.insert(QStringLiteral("evidence_fragments"),
                                  analyzed.value().evidenceFragmentCount);
                    result.insert(QStringLiteral("analysis_chunks"),
                                  analyzed.value().analyzedChunkCount);
                    result.insert(QStringLiteral("candidate_methods"),
                                  static_cast<int>(analyzed.value().candidateMethods.size()));
                    result.insert(QStringLiteral("candidate_tips"),
                                  static_cast<int>(analyzed.value().candidateTips.size()));
                } else {
                    result.insert(QStringLiteral("analysis_status"), QStringLiteral("failed"));
                    result.insert(QStringLiteral("analysis_message"),
                                  QString::fromStdString(analyzed.error().message));
                    ++analysisFailures;
                }
            }
        }
        results.append(result);

        if (outcome.status == "imported")
            ++imported;
        else if (outcome.status == "duplicate")
            ++duplicates;
        else
            ++failed;
        std::printf("[%s] %s\n", outcome.status.c_str(), item.source.c_str());
    }

    QJsonObject finalReport;
    QJsonObject runMetadata;
    runMetadata.insert(QStringLiteral("report_schema"), QStringLiteral("corpus-report-v2"));
    runMetadata.insert(QStringLiteral("generated_at"), QString::fromStdString(clock.utcIso()));
    runMetadata.insert(QStringLiteral("index_directory"), options.indexDirPath);
    runMetadata.insert(QStringLiteral("sample_size"), options.sampleSize);
    runMetadata.insert(QStringLiteral("target_successes"), options.targetSuccesses);
    runMetadata.insert(QStringLiteral("analysis_requested"), options.analyze);
    runMetadata.insert(QStringLiteral("pdftotext_path"), options.pdfToTextPath);
    runMetadata.insert(QStringLiteral("pdftoppm_path"), options.pdfToPpmPath);
    runMetadata.insert(QStringLiteral("tesseract_path"), options.tesseractPath);
    finalReport.insert(QStringLiteral("run_metadata"), runMetadata);
    finalReport.insert(QStringLiteral("results"), results);
    finalReport.insert(QStringLiteral("imported"), imported);
    finalReport.insert(QStringLiteral("failed"), failed + analysisFailures);
    finalReport.insert(QStringLiteral("pipeline_failed"), failed);
    finalReport.insert(QStringLiteral("analysis_failed"), analysisFailures);
    finalReport.insert(QStringLiteral("duplicates"), duplicates);
    QJsonObject summary;
    summary.insert(QStringLiteral("selected"), static_cast<int>(totalItems));
    summary.insert(QStringLiteral("attempted"), imported + failed + duplicates);
    summary.insert(QStringLiteral("resumed_skipped"), resumedSkipped);
    summary.insert(QStringLiteral("pipeline_imported"), imported);
    summary.insert(QStringLiteral("pipeline_failed"), failed);
    summary.insert(QStringLiteral("duplicates"), duplicates);
    const int attempted = imported + failed + duplicates;
    summary.insert(QStringLiteral("success_rate"),
                   attempted > 0 ? static_cast<double>(imported) / attempted : 0.0);
    QSet<QString> successfulDomains;
    QJsonObject formats;
    QJsonObject extractors;
    QJsonObject failureCategories;
    int analysisCompleted = 0;
    int analysisFailed = 0;
    const auto increment = [](QJsonObject &object, const QString &key) {
        object.insert(key, object.value(key).toInt() + 1);
    };
    for (qsizetype i = runResultStart; i < results.size(); ++i) {
        const QJsonObject value = results.at(i).toObject();
        if (value.value(QStringLiteral("status")).toString() == QStringLiteral("imported")) {
            successfulDomains.insert(value.value(QStringLiteral("domain_code")).toString());
            const QString path = value.value(QStringLiteral("asset_path")).toString();
            increment(formats,
                      path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)
                          ? QStringLiteral("pdf")
                          : path.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive)
                                ? QStringLiteral("structured_xml")
                                : QStringLiteral("structured_json"));
            increment(extractors, value.value(QStringLiteral("extraction_tool")).toString(
                                      QStringLiteral("unknown")));
        } else {
            const QString message = value.value(QStringLiteral("message")).toString();
            QString category = QStringLiteral("other");
            if (message.contains(QStringLiteral("HTTP 403"))) category = QStringLiteral("http_403");
            else if (message.contains(QStringLiteral("HTML instead of PDF"))) category = QStringLiteral("html_instead_of_pdf");
            else if (message.contains(QStringLiteral("extraction failed"))) category = QStringLiteral("extraction_failed");
            else if (message.contains(QStringLiteral("file open failed"))) category = QStringLiteral("file_open_failed");
            increment(failureCategories, category);
        }
        const QString analysisStatus = value.value(QStringLiteral("analysis_status")).toString();
        if (analysisStatus == QStringLiteral("completed")) ++analysisCompleted;
        else if (analysisStatus == QStringLiteral("failed")) ++analysisFailed;
    }
    summary.insert(QStringLiteral("successful_domain_count"), successfulDomains.size());
    summary.insert(QStringLiteral("formats"), formats);
    summary.insert(QStringLiteral("extractors"), extractors);
    summary.insert(QStringLiteral("failure_categories"), failureCategories);
    summary.insert(QStringLiteral("analysis_completed"), analysisCompleted);
    summary.insert(QStringLiteral("analysis_failed"), analysisFailed);
    finalReport.insert(QStringLiteral("run_summary"), summary);
    if (!saveReport(options.reportPath, finalReport)) {
        std::fprintf(stderr, "报告或校验文件写入失败: %s\n", qPrintable(options.reportPath));
        return 4;
    }

    // 索引一致性（失败不静默）
    Infrastructure::SqlKnowledgeFtsIndex fts(DatabaseManager::instance().database(), clock);
    if (!fts.isConsistent()) {
        const auto rebuilt = fts.rebuildAll();
        if (!rebuilt.ok)
            std::fprintf(stderr, "FTS 重建失败: %s\n",
                         qPrintable(QString::fromStdString(rebuilt.error.message)));
    }

    std::printf("完成：导入 %d，管道失败 %d，分析失败 %d，重复 %d\n",
                imported, failed, analysisFailures, duplicates);
    return failed + analysisFailures == 0 ? 0 : 2;
}
