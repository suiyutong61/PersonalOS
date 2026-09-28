// 模块：开发者批量文献脚手架（DR-009；scope-v1 §5 工程交付物）
// 覆盖：文本/PDF 条目经单文献处理核心导入（正式结构）、同一内容去重、
//       失败条目明确报告、知识包导出/导入往返（哈希完整性校验）。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>

#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/CorpusPipeline.h"
#include "infrastructure/knowledge/KnowledgePackage.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/knowledge/SqlKnowledgeFtsIndex.h"

using namespace PersonOS;

namespace {

std::string minimalPdf(const std::string &content)
{
    std::string pdf = "%PDF-1.4\n";
    pdf += "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n";
    pdf += "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n";
    pdf += "3 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
           "/Contents 4 0 R >> endobj\n";
    pdf += "4 0 obj << /Length " + std::to_string(content.size()) + " >> stream\n"
           + content + "\nendstream endobj\n";
    pdf += "trailer << /Root 1 0 R /Size 5 >>\n";
    return pdf;
}

} // namespace

class TstCorpusScaffold : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_corpus.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
        QVERIFY(Infrastructure::LearningManifestSeed(DatabaseManager::instance().database(),
                                                     m_clock)
                    .ensureSeeded());
    }

    void textAndPdfItemsImportThroughCore()
    {
        // 文本条目
        QFile textFile(QDir::temp().filePath(QStringLiteral("corpus_sample.txt")));
        QVERIFY(textFile.open(QIODevice::WriteOnly | QIODevice::Text));
        textFile.write("间隔复习能够提升长期保持率：分散练习优于集中练习。");
        textFile.close();

        Infrastructure::CorpusPipeline pipeline(DatabaseManager::instance().database(),
                                                m_uids, m_clock);
        Infrastructure::CorpusPipeline::Item textItem;
        textItem.source = textFile.fileName().toStdString();
        textItem.title = "间隔复习研究";
        textItem.kind = "text";
        const auto textOutcome = pipeline.process(textItem);
        QVERIFY2(textOutcome.ok, textOutcome.message.c_str());
        QCOMPARE(textOutcome.status, std::string("imported"));
        QVERIFY(!textOutcome.itemUid.empty());
        QVERIFY(!textOutcome.sourceUid.empty());
        QVERIFY(!textOutcome.extractedText.empty());

        // PDF 条目（最小 PDF 经提取器 → 论文导入）
        QFile pdfFile(QDir::temp().filePath(QStringLiteral("corpus_sample.pdf")));
        QVERIFY(pdfFile.open(QIODevice::WriteOnly));
        pdfFile.write(QByteArray::fromStdString(
            minimalPdf("BT (Spaced retrieval improves retention) Tj ET")));
        pdfFile.close();

        Infrastructure::CorpusPipeline::Item pdfItem;
        pdfItem.source = pdfFile.fileName().toStdString();
        pdfItem.title = "Spaced Retrieval";
        pdfItem.kind = "paper";
        const auto pdfOutcome = pipeline.process(pdfItem);
        QVERIFY2(pdfOutcome.ok, pdfOutcome.message.c_str());
        QCOMPARE(pdfOutcome.status, std::string("imported"));
        QVERIFY(!pdfOutcome.assetUid.empty());
        QVERIFY(pdfOutcome.assetRelativePath.ends_with(".pdf"));
        QSqlQuery assets(DatabaseManager::instance().database());
        assets.prepare(QStringLiteral(
            "SELECT storage_state, relative_path FROM file_assets_v5 WHERE uid=?"));
        assets.addBindValue(QString::fromStdString(pdfOutcome.assetUid));
        QVERIFY(assets.exec());
        QVERIFY(assets.next());
        QCOMPARE(assets.value(0).toString(), QStringLiteral("managed"));
        QVERIFY(QFileInfo::exists(QDir(QFileInfo(m_path).absolutePath())
                                     .filePath(assets.value(1).toString())));

        // 同一 PDF 再次处理 → duplicate（内容指纹去重）
        const auto duplicate = pipeline.process(pdfItem);
        QCOMPARE(duplicate.status, std::string("duplicate"));

        // 失败条目：不存在的文件 → 明确失败
        Infrastructure::CorpusPipeline::Item missing;
        missing.source = QDir::temp().filePath(QStringLiteral("no_such_file.pdf")).toStdString();
        const auto failed = pipeline.process(missing);
        QCOMPARE(failed.status, std::string("failed"));
        QVERIFY(!failed.message.empty());

        // 正式库中论文与方法结构一致（共享单文献处理核心）
        QSqlQuery count(DatabaseManager::instance().database());
        QVERIFY(count.exec(QStringLiteral(
            "SELECT COUNT(*) FROM knowledge_items_v5 WHERE library_type='paper'")));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), 2);

        // 索引元数据进入正式论文/来源结构，领域不再固定为 learning。
        QFile indexedFile(QDir::temp().filePath(QStringLiteral("indexed_paper.txt")));
        QVERIFY(indexedFile.open(QIODevice::WriteOnly | QIODevice::Text));
        indexedFile.write("A real indexed paper body.");
        indexedFile.close();
        Infrastructure::CorpusPipeline::Item indexed;
        indexed.source = indexedFile.fileName().toStdString();
        indexed.title = "Indexed paper";
        indexed.domainCode = "research.43.culture-equity";
        indexed.paperNumber = "43.1";
        indexed.publishedYear = "2024";
        indexed.studyType = "Systematic Review";
        indexed.indexSourceFile = "43-Culture-Equity.md";
        indexed.indexStartLine = 42;
        indexed.indexEndLine = 51;
        const auto indexedOutcome = pipeline.process(indexed);
        QVERIFY2(indexedOutcome.ok, indexedOutcome.message.c_str());
        QSqlQuery metadata(DatabaseManager::instance().database());
        QVERIFY(metadata.exec(QStringLiteral(
            "SELECT i.domain_code,p.study_type,s.published_at,s.metadata_json "
            "FROM knowledge_items_v5 i JOIN papers_v5 p ON p.knowledge_item_id=i.id "
            "JOIN source_records_v5 s ON s.title=i.title WHERE i.title='Indexed paper'")));
        QVERIFY(metadata.next());
        QCOMPARE(metadata.value(0).toString(), QStringLiteral("research.43.culture-equity"));
        QCOMPARE(metadata.value(1).toString(), QStringLiteral("Systematic Review"));
        QCOMPARE(metadata.value(2).toString(), QStringLiteral("2024"));
        QVERIFY(metadata.value(3).toString().contains(QStringLiteral("43-Culture-Equity.md")));
    }

    void packageRoundTrip()
    {
        Infrastructure::KnowledgePackageExporter exporter(
            DatabaseManager::instance().database(), m_clock);
        const auto package = exporter.exportToJson();
        QVERIFY2(package.hasValue(),
                 package.hasValue() ? "" : package.error().message.c_str());
        Infrastructure::KnowledgePackageImporter importer(
            DatabaseManager::instance().database(), m_clock);
        const auto report = importer.importFromJson(package.value());
        QVERIFY2(report.hasValue(),
                 report.hasValue() ? "" : report.error().message.c_str());
        QCOMPARE(report.value().importedItems, 0);
        QVERIFY(report.value().skippedExisting >= 2);
        std::string tampered = package.value();
        const auto pos = tampered.find("paper");
        if (pos != std::string::npos)
            tampered[pos] = tampered[pos] == 'p' ? 'x' : 'p';
        const auto rejected = importer.importFromJson(tampered);
        QVERIFY(!rejected);
        QVERIFY(rejected.error().message.find("hash") != std::string::npos);
    }

    void multipleLocalSourcesFallBack()
    {
        QFile good(QDir::temp().filePath(QStringLiteral("fallback_source.txt")));
        QVERIFY(good.open(QIODevice::WriteOnly | QIODevice::Text));
        good.write("fallback content");
        good.close();
        Infrastructure::CorpusPipeline pipeline(DatabaseManager::instance().database(),
                                                m_uids, m_clock);
        Infrastructure::CorpusPipeline::Item item;
        item.title = "Fallback source";
        item.kind = "text";
        item.domainCode = "research.01.goal-self-regulation";
        item.sourceCandidates = {"Z:/definitely/missing.txt", good.fileName().toStdString()};
        const auto result = pipeline.process(item);
        QVERIFY2(result.ok, result.message.c_str());
        QCOMPARE(QString::fromStdString(result.resolvedSource), good.fileName());
        QCOMPARE(result.downloadAttempts.size(), size_t(2));
        QVERIFY(!result.downloadAttempts.front().success);
        QVERIFY(result.downloadAttempts.back().success);
    }

    void bioCFullTextFallbackImportsStructuredPaper()
    {
        const QString path = QDir::temp().filePath(QStringLiteral("sample.bioc.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(R"JSON([{"documents":[{"passages":[
          {"infons":{"section_type":"TITLE"},"text":"A structured paper"},
          {"infons":{"section_type":"ABSTRACT"},"text":"This is a sufficiently long abstract for reliable structured full text import and indexing."},
          {"infons":{"section_type":"REF"},"text":"Reference should be excluded"}
        ]}]}])JSON");
        file.close();
        Infrastructure::CorpusPipeline pipeline(DatabaseManager::instance().database(),
                                                m_uids, m_clock);
        Infrastructure::CorpusPipeline::Item item;
        item.source = path.toStdString();
        item.title = "BioC paper";
        item.kind = "paper";
        item.domainCode = "research.08.exercise";
        const auto result = pipeline.process(item);
        QVERIFY2(result.ok, result.message.c_str());
        QVERIFY(result.assetRelativePath.ends_with(".json"));
        QVERIFY(result.extractedText.find("sufficiently long abstract") != std::string::npos);
        QVERIFY(result.extractedText.find("Reference should be excluded") == std::string::npos);
        QSqlQuery version(DatabaseManager::instance().database());
        version.prepare(QStringLiteral(
            "SELECT extraction_model FROM knowledge_versions_v5 v "
            "JOIN knowledge_items_v5 i ON i.current_version_uid=v.uid WHERE i.uid=?"));
        version.addBindValue(QString::fromStdString(result.itemUid));
        QVERIFY(version.exec());
        QVERIFY(version.next());
        QCOMPARE(version.value(0).toString(), QStringLiteral("ncbi-bioc-json"));
    }

    void europePmcFullTextXmlImportsStructuredPaper()
    {
        const QString path = QDir::temp().filePath(QStringLiteral("sample.fulltext.xml"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(R"XML(<?xml version="1.0" encoding="UTF-8"?>
<article>
  <front><article-meta><article-title>An XML structured paper</article-title></article-meta></front>
  <body>
    <sec><title>Results</title>
      <p>This is a sufficiently long paragraph from the Europe PMC full text XML for reliable structured import and indexing.</p>
      <p>A second paragraph with more detail about the findings.</p>
    </sec>
  </body>
  <back><ref-list><ref><mixed-citation>Reference text should be excluded from the extracted body.</mixed-citation></ref></ref-list></back>
</article>)XML");
        file.close();
        Infrastructure::CorpusPipeline pipeline(DatabaseManager::instance().database(),
                                                m_uids, m_clock);
        Infrastructure::CorpusPipeline::Item item;
        item.source = path.toStdString();
        item.title = "XML paper";
        item.kind = "paper";
        item.domainCode = "research.08.exercise";
        const auto result = pipeline.process(item);
        QVERIFY2(result.ok, result.message.c_str());
        QVERIFY(result.assetRelativePath.ends_with(".xml"));
        QVERIFY(result.extractedText.find("sufficiently long paragraph") != std::string::npos);
        QVERIFY(result.extractedText.find("Reference text should be excluded") == std::string::npos);
        QSqlQuery version(DatabaseManager::instance().database());
        version.prepare(QStringLiteral(
            "SELECT extraction_model FROM knowledge_versions_v5 v "
            "JOIN knowledge_items_v5 i ON i.current_version_uid=v.uid WHERE i.uid=?"));
        version.addBindValue(QString::fromStdString(result.itemUid));
        QVERIFY(version.exec());
        QVERIFY(version.next());
        QCOMPARE(version.value(0).toString(), QStringLiteral("europe-pmc-fulltext-xml"));
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstCorpusScaffold)
#include "tst_corpus_scaffold.moc"
