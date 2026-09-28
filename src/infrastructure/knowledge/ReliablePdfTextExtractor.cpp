#include "infrastructure/knowledge/ReliablePdfTextExtractor.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "infrastructure/knowledge/SimplePdfTextExtractor.h"

namespace PersonOS::Infrastructure {
namespace {

QString toolPath(const char *environmentName, const QString &program)
{
    const QString configured = qEnvironmentVariable(environmentName);
    return configured.isEmpty() ? QStandardPaths::findExecutable(program) : configured;
}

QString versionOf(const QString &program, const QStringList &arguments)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(10000))
        return QStringLiteral("unknown");
    QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (output.isEmpty())
        output = QString::fromUtf8(process.readAllStandardError()).trimmed();
    return output.section(QLatin1Char('\n'), 0, 0).left(200);
}

bool meaningful(const QByteArray &text)
{
    QByteArray compact = text;
    compact.replace('\r', ' ');
    compact.replace('\n', ' ');
    return compact.simplified().size() >= 80;
}

} // namespace

std::optional<PdfExtractionResult> ReliablePdfTextExtractor::extract(
    const std::string &pdfBytes, std::string *error)
{
    if (pdfBytes.size() < 8 || pdfBytes.compare(0, 5, "%PDF-") != 0) {
        if (error) *error = "not a PDF file";
        return std::nullopt;
    }
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        if (error) *error = "cannot create PDF extraction workspace";
        return std::nullopt;
    }
    const QString inputPath = temporary.filePath(QStringLiteral("input.pdf"));
    QFile input(inputPath);
    if (!input.open(QIODevice::WriteOnly)
        || input.write(pdfBytes.data(), static_cast<qint64>(pdfBytes.size())) < 0) {
        if (error) *error = "cannot stage PDF for extraction";
        return std::nullopt;
    }
    input.close();

    PdfExtractionResult result;
    const QString pdftotext = toolPath("PERSONOS_PDFTOTEXT_PATH",
                                       QStringLiteral("pdftotext"));
    if (!pdftotext.isEmpty()) {
        QProcess process;
        process.start(pdftotext, {QStringLiteral("-layout"), QStringLiteral("-enc"),
                                  QStringLiteral("UTF-8"), inputPath, QStringLiteral("-")});
        if (process.waitForFinished(120000) && process.exitCode() == 0) {
            const QByteArray text = process.readAllStandardOutput();
            if (meaningful(text)) {
                result.text = text.toStdString();
                result.extractor = "poppler-pdftotext";
                result.extractorVersion =
                    versionOf(pdftotext, {QStringLiteral("-v")}).toStdString();
                return result;
            }
            result.warnings.push_back("pdftotext returned insufficient text; OCR considered");
        } else {
            result.warnings.push_back("pdftotext failed: "
                                      + process.readAllStandardError().toStdString());
        }
    } else {
        result.warnings.push_back("pdftotext unavailable");
    }

    const QString pdftoppm = toolPath("PERSONOS_PDFTOPPM_PATH", QStringLiteral("pdftoppm"));
    const QString tesseract = toolPath("PERSONOS_TESSERACT_PATH", QStringLiteral("tesseract"));
    if (!pdftoppm.isEmpty() && !tesseract.isEmpty()) {
        const QString prefix = temporary.filePath(QStringLiteral("page"));
        QProcess raster;
        raster.start(pdftoppm, {QStringLiteral("-png"), QStringLiteral("-r"),
                                QStringLiteral("300"), inputPath, prefix});
        if (raster.waitForFinished(300000) && raster.exitCode() == 0) {
            QByteArray combined;
            const auto pages = QDir(temporary.path()).entryList(
                {QStringLiteral("page-*.png")}, QDir::Files, QDir::Name);
            for (const QString &page : pages) {
                QProcess ocr;
                ocr.start(tesseract, {temporary.filePath(page), QStringLiteral("stdout"),
                                      QStringLiteral("-l"), QStringLiteral("eng+chi_sim"),
                                      QStringLiteral("--psm"), QStringLiteral("3")});
                if (ocr.waitForFinished(180000) && ocr.exitCode() == 0) {
                    combined += ocr.readAllStandardOutput();
                    combined += '\n';
                } else {
                    result.warnings.push_back("OCR failed for " + page.toStdString());
                }
            }
            if (meaningful(combined)) {
                result.text = combined.toStdString();
                result.extractor = "tesseract-ocr";
                result.extractorVersion =
                    versionOf(tesseract, {QStringLiteral("--version")}).toStdString();
                result.ocrUsed = true;
                return result;
            }
        }
    } else {
        result.warnings.push_back("OCR unavailable (pdftoppm and/or tesseract missing)");
    }

    std::string fallbackError;
    const auto fallback = SimplePdfTextExtractor::extractText(pdfBytes, &fallbackError);
    if (fallback) {
        result.text = *fallback;
        result.extractor = "personos-simple-pdf-fallback";
        result.extractorVersion = "1";
        result.warnings.push_back("used limited built-in PDF fallback");
        return result;
    }
    if (error) {
        *error = "all PDF extraction paths failed: " + fallbackError;
        for (const auto &warning : result.warnings)
            *error += "; " + warning;
    }
    return std::nullopt;
}

} // namespace PersonOS::Infrastructure
