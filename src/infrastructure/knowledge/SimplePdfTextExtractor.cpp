#include "infrastructure/knowledge/SimplePdfTextExtractor.h"

#include <QByteArray>

#include <map>
#include <string>
#include <vector>

namespace PersonOS::Infrastructure {

namespace {

std::optional<std::string> fail(std::string *error, const std::string &message)
{
    if (error)
        *error = message;
    return std::nullopt;
}

bool isPdfWhitespace(char c)
{
    return c == 0 || c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ';
}

// 跳过空白与注释；返回 false 表示到达结尾
bool skipWsAndComments(const std::string &data, size_t &pos)
{
    while (pos < data.size()) {
        if (isPdfWhitespace(data[pos])) {
            ++pos;
        } else if (data[pos] == '%') {
            while (pos < data.size() && data[pos] != '\n' && data[pos] != '\r')
                ++pos;
        } else {
            return true;
        }
    }
    return false;
}

// 解析一个 PDF 对象（标量/名称/字符串/数组/字典/十六进制字符串）
// 返回原始文本区间或解析后的字典/流区间；为简化，这里只实现
// 字符串/名称/数字/数组/字典的浅层扫描。
std::string parseToken(const std::string &data, size_t &pos)
{
    skipWsAndComments(data, pos);
    if (pos >= data.size())
        return {};
    if (data[pos] == '/') {   // 名称
        ++pos;
        const size_t start = pos;
        while (pos < data.size() && !isPdfWhitespace(data[pos])
               && data[pos] != '/' && data[pos] != '[' && data[pos] != ']'
               && data[pos] != '(' && data[pos] != ')' && data[pos] != '<'
               && data[pos] != '>' && data[pos] != '{' && data[pos] != '}')
            ++pos;
        return data.substr(start, pos - start);
    }
    if (data[pos] == '(') {   // 字面字符串（支持转义与嵌套括号）
        ++pos;
        std::string out;
        int depth = 1;
        while (pos < data.size() && depth > 0) {
            const char c = data[pos++];
            if (c == '\\' && pos < data.size()) {
                const char escaped = data[pos++];
                switch (escaped) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case '(': case ')': case '\\': out += escaped; break;
                default: out += escaped; break;
                }
            } else if (c == '(') {
                ++depth;
                out += c;
            } else if (c == ')') {
                --depth;
                if (depth > 0)
                    out += c;
            } else {
                out += c;
            }
        }
        return out;
    }
    if (data[pos] == '<') {   // 十六进制字符串（跳过）
        while (pos < data.size() && data[pos] != '>')
            ++pos;
        if (pos < data.size())
            ++pos;
        return {};
    }
    // 数字/关键字：读到分隔符
    const size_t start = pos;
    while (pos < data.size() && !isPdfWhitespace(data[pos]) && data[pos] != '/'
           && data[pos] != '[' && data[pos] != ']' && data[pos] != '('
           && data[pos] != ')' && data[pos] != '<' && data[pos] != '>')
        ++pos;
    return data.substr(start, pos - start);
}

// 提取内容流中的可见文本：处理 Tj / TJ / ' / " 操作符
std::string extractTextFromContent(const std::string &content)
{
    std::string out;
    size_t pos = 0;
    while (pos < content.size()) {
        // 找下一个 '('
        const size_t open = content.find('(', pos);
        if (open == std::string::npos)
            break;
        size_t p = open;
        std::string text = parseToken(content, p);
        // 解析完成后，看其后的操作符是否为 Tj / ' / "（TJ 数组内的字符串也收集）
        skipWsAndComments(content, p);
        if (p < content.size() && (content.compare(p, 2, "Tj") == 0
                                   || content[p] == '\'' || content[p] == '"')) {
            if (!out.empty() && out.back() != '\n')
                out += ' ';
            out += text;
            pos = p;
            continue;
        }
        pos = open + 1;
    }
    return out;
}

} // namespace

std::optional<std::string> SimplePdfTextExtractor::extractText(const std::string &pdfBytes,
                                                              std::string *error)
{
    if (pdfBytes.size() < 8 || pdfBytes.compare(0, 5, "%PDF-") != 0)
        return fail(error, "not a PDF file");
    if (pdfBytes.find("/Encrypt") != std::string::npos)
        return fail(error, "encrypted PDF is not supported");

    std::string text;
    size_t pos = 0;
    // 扫描所有 stream ... endstream（内容流与其他流都尝试）
    bool foundAny = false;
    while (true) {
        const size_t streamPos = pdfBytes.find("stream", pos);
        if (streamPos == std::string::npos)
            break;
        const size_t endstreamPos = pdfBytes.find("endstream", streamPos);
        if (endstreamPos == std::string::npos)
            return fail(error, "malformed PDF: missing endstream");

        // 流参数（字典）在 stream 前：检查 /Filter
        const size_t dictStart = pdfBytes.rfind("<<", streamPos);
        const size_t dictEnd = streamPos;
        std::string filter;
        if (dictStart != std::string::npos && dictStart < dictEnd) {
            const std::string dict = pdfBytes.substr(dictStart, dictEnd - dictStart);
            const size_t filterPos = dict.find("/Filter");
            if (filterPos != std::string::npos) {
                size_t fp = filterPos + 7;
                const std::string filterName = parseToken(dict, fp);
                filter = filterName;
            }
        }

        size_t dataStart = streamPos + 6;
        // stream 关键字后跟 \r\n 或 \n
        if (dataStart < pdfBytes.size() && pdfBytes[dataStart] == '\r')
            ++dataStart;
        if (dataStart < pdfBytes.size() && pdfBytes[dataStart] == '\n')
            ++dataStart;
        size_t dataEnd = endstreamPos;
        while (dataEnd > dataStart && (pdfBytes[dataEnd - 1] == '\r'
                                       || pdfBytes[dataEnd - 1] == '\n'))
            --dataEnd;
        const std::string raw = pdfBytes.substr(dataStart, dataEnd - dataStart);

        std::string content;
        if (filter == "FlateDecode" || filter == "Fl") {
            const QByteArray decompressed = qUncompress(
                QByteArray::fromRawData(raw.data(), static_cast<int>(raw.size())));
            if (decompressed.isEmpty() && !raw.empty())
                return fail(error, "FlateDecode decompression failed");
            content = std::string(decompressed.constData(),
                                  static_cast<size_t>(decompressed.size()));
        } else if (filter.empty()) {
            content = raw;
        } else {
            // 其他过滤器（如 ASCIIHexDecode 内容流少见）：按内容流尝试失败处理
            content = raw;
        }

        if (!content.empty()) {
            foundAny = true;
            const std::string chunk = extractTextFromContent(content);
            if (!chunk.empty()) {
                if (!text.empty() && text.back() != '\n')
                    text += '\n';
                text += chunk;
            }
        }
        pos = endstreamPos + 9;
    }

    if (!foundAny)
        return fail(error, "no content streams found");
    if (text.empty())
        return fail(error, "no extractable text (scanned image PDF?)");
    return text;
}

} // namespace PersonOS::Infrastructure
