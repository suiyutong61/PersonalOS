// 模块：最小 PDF 文本提取器（脚手架/单文献处理核心共用）
// 覆盖：未压缩内容流、FlateDecode 流、Tj 文本拼接、非 PDF 拒绝、
//       加密 PDF 明确失败、无文本 PDF 明确失败（不猜测内容）。
#include <QtTest>

#include <QByteArray>

#include "infrastructure/knowledge/SimplePdfTextExtractor.h"
#include "infrastructure/knowledge/ReliablePdfTextExtractor.h"

using namespace PersonOS;

namespace {

std::string pdfWithContent(const std::string &content, const std::string &filter)
{
    std::string pdf = "%PDF-1.4\n";
    pdf += "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n";
    pdf += "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n";
    pdf += "3 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
           "/Contents 4 0 R >> endobj\n";
    pdf += "4 0 obj << /Length " + std::to_string(content.size());
    if (!filter.empty())
        pdf += " /Filter /" + filter;
    pdf += " >> stream\n" + content + "\nendstream endobj\n";
    pdf += "trailer << /Root 1 0 R /Size 5 >>\n";
    return pdf;
}

} // namespace

class TstPdfExtractor : public QObject
{
    Q_OBJECT

private slots:
    void uncompressedStream()
    {
        const std::string pdf = pdfWithContent(
            "BT /F1 12 Tf 72 720 Td (Hello PDF 你好世界) Tj ET", "");
        std::string error;
        const auto text = Infrastructure::SimplePdfTextExtractor::extractText(pdf, &error);
        QVERIFY2(text.has_value(), error.c_str());
        QVERIFY(text->find("Hello PDF") != std::string::npos);
        QVERIFY(text->find("你好世界") != std::string::npos);
    }

    void flateDecodeStream()
    {
        const std::string content = "BT (Interval retrieval improves retention) Tj ET";
        const QByteArray compressed =
            qCompress(QByteArray::fromStdString(content));   // zlib 格式
        const std::string pdf =
            pdfWithContent(std::string(compressed.constData(), compressed.size()),
                           "FlateDecode");
        std::string error;
        const auto text = Infrastructure::SimplePdfTextExtractor::extractText(pdf, &error);
        QVERIFY2(text.has_value(), error.c_str());
        QVERIFY(text->find("Interval retrieval") != std::string::npos);
    }

    void invalidInputsFailCleanly()
    {
        std::string error;
        QVERIFY(!Infrastructure::SimplePdfTextExtractor::extractText("not a pdf", &error)
                     .has_value());
        QVERIFY(!error.empty());

        // 加密 PDF：明确失败
        const std::string encrypted = "%PDF-1.4\n1 0 obj << /Encrypt 5 0 R >> endobj\n";
        QVERIFY(!Infrastructure::SimplePdfTextExtractor::extractText(encrypted, &error)
                     .has_value());

        // 只有图像（无文本流内容）：明确失败，不猜测
        const std::string imageOnly =
            pdfWithContent("q 0 0 612 792 re W n Q", "");   // 无 Tj 文本
        QVERIFY(!Infrastructure::SimplePdfTextExtractor::extractText(imageOnly, &error)
                     .has_value());
        QVERIFY(error.find("text") != std::string::npos);
    }

    void reliableExtractorRejectsNonPdf()
    {
        std::string error;
        const auto result = Infrastructure::ReliablePdfTextExtractor::extract("not pdf", &error);
        QVERIFY(!result);
        QVERIFY(!error.empty());
    }
};

QTEST_GUILESS_MAIN(TstPdfExtractor)
#include "tst_pdf_extractor.moc"
