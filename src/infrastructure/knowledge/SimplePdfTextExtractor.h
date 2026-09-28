#pragma once

#include <optional>
#include <string>

// 最小 PDF 文本提取器（脚手架/单文献处理核心共用；不依赖第三方 PDF 库）
// 支持：对象解析、FlateDecode 流解压（zlib）、内容流 Tj/TJ 文本拼接。
// 不支持加密/损坏/罕见编码时明确失败（返回错误信息，不猜测内容）。
namespace PersonOS::Infrastructure {

class SimplePdfTextExtractor
{
public:
    // pdfBytes → 提取文本；失败返回错误说明
    static std::optional<std::string> extractText(const std::string &pdfBytes,
                                                  std::string *error = nullptr);
};

} // namespace PersonOS::Infrastructure
