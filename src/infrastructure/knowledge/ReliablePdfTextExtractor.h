#pragma once

#include <optional>
#include <string>
#include <vector>

namespace PersonOS::Infrastructure {

struct PdfExtractionResult
{
    std::string text;
    std::string extractor;
    std::string extractorVersion;
    bool ocrUsed = false;
    std::vector<std::string> warnings;
};

// 真实 PDF 主路径：Poppler pdftotext；无文本时可选 pdftoppm + Tesseract OCR；
// 最小内置解析器仅作最后回退。外部工具可通过 PERSONOS_*_PATH 指定。
class ReliablePdfTextExtractor
{
public:
    static std::optional<PdfExtractionResult> extract(const std::string &pdfBytes,
                                                      std::string *error = nullptr);
};

} // namespace PersonOS::Infrastructure
