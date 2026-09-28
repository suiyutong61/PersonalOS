#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

// CJK 二元分词（SQLite FTS5 unicode61 不切分中文，整段视为单个 token；
// 索引与查询两侧采用同一扩展规则，保证词法召回对中文可用）。
// 索引侧：CJK 连续段展开为空格分隔的 bigram（单字段保留单字）。
// 查询侧：CJK 连续段展开为 ("bg1" OR "bg2" ...) 的 OR 词组。
namespace PersonOS::Infrastructure::Cjk {

inline bool isCjkCodepoint(unsigned int codepoint)
{
    return (codepoint >= 0x3400 && codepoint <= 0x4DBF)
           || (codepoint >= 0x4E00 && codepoint <= 0x9FFF)
           || (codepoint >= 0xF900 && codepoint <= 0xFAFF);
}

// UTF-8 解码下一码点；结束返回 0
inline unsigned int nextCodepoint(std::string_view text, size_t &offset)
{
    if (offset >= text.size())
        return 0;
    const unsigned char c = static_cast<unsigned char>(text[offset]);
    if (c < 0x80) {
        ++offset;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && offset + 1 < text.size()) {
        const unsigned int value = ((c & 0x1F) << 6)
                                   | (static_cast<unsigned char>(text[offset + 1]) & 0x3F);
        offset += 2;
        return value;
    }
    if ((c & 0xF0) == 0xE0 && offset + 2 < text.size()) {
        const unsigned int value = ((c & 0x0F) << 12)
                                   | ((static_cast<unsigned char>(text[offset + 1]) & 0x3F)
                                      << 6)
                                   | (static_cast<unsigned char>(text[offset + 2]) & 0x3F);
        offset += 3;
        return value;
    }
    if ((c & 0xF8) == 0xF0 && offset + 3 < text.size()) {
        const unsigned int value = ((c & 0x07) << 18)
                                   | ((static_cast<unsigned char>(text[offset + 1]) & 0x3F)
                                      << 12)
                                   | ((static_cast<unsigned char>(text[offset + 2]) & 0x3F)
                                      << 6)
                                   | (static_cast<unsigned char>(text[offset + 3]) & 0x3F);
        offset += 4;
        return value;
    }
    ++offset;   // 非法序列：跳过一字节
    return 0xFFFD;
}

// 索引侧：CJK 段 → 空格分隔 bigram；非 CJK 原样保留
inline std::string expandForIndex(const std::string &text)
{
    std::string out;
    std::string run;
    auto flushRun = [&out](std::string &run) {
        if (run.empty())
            return;
        if (run.size() <= 3) {   // 单字（UTF-8 3 字节）
            if (!out.empty() && out.back() != ' ')
                out += ' ';
            out += run;
            out += ' ';
        } else {
            for (size_t i = 0; i + 3 < run.size(); i += 3) {
                out += run.substr(i, 6);
                out += ' ';
            }
        }
        run.clear();
    };

    size_t offset = 0;
    while (offset < text.size()) {
        const size_t start = offset;
        const unsigned int codepoint = nextCodepoint(text, offset);
        if (isCjkCodepoint(codepoint)) {
            run.append(text.substr(start, offset - start));
        } else {
            flushRun(run);
            out.append(text.substr(start, offset - start));
        }
    }
    flushRun(run);
    return out;
}

// 查询侧：CJK 段 → ("bg1" OR "bg2" ...)，多个段用 AND 连接；
// 非 CJK 段保留字母数字 token；查询含 FTS 特殊语法（*、" 等）时原样透传。
inline std::string expandForQuery(const std::string &text)
{
    if (text.find('*') != std::string::npos || text.find('"') != std::string::npos)
        return text;   // 前缀/短语等高级语法：保持原样（bigram 索引同样支持前缀）

    std::vector<std::string> segments;
    std::string run;
    auto flushRun = [&segments](std::string &run) {
        if (run.empty())
            return;
        if (run.size() <= 3) {
            segments.push_back("\"" + run + "\"");
        } else {
            std::string group = "(";
            bool first = true;
            for (size_t i = 0; i + 3 < run.size(); i += 3) {
                if (!first)
                    group += " OR ";
                first = false;
                group += "\"" + run.substr(i, 6) + "\"";
            }
            group += ")";
            segments.push_back(group);
        }
        run.clear();
    };
    auto flushPlain = [&segments](std::string &plain) {
        std::string trimmed;
        for (const char c : plain)
            trimmed += (std::isalnum(static_cast<unsigned char>(c)) || c == ' ') ? c : ' ';
        // 收尾空白清理
        while (!trimmed.empty() && trimmed.back() == ' ')
            trimmed.pop_back();
        if (!trimmed.empty())
            segments.push_back(trimmed);
        plain.clear();
    };

    std::string plain;
    size_t offset = 0;
    while (offset < text.size()) {
        const size_t start = offset;
        const unsigned int codepoint = nextCodepoint(text, offset);
        if (isCjkCodepoint(codepoint)) {
            flushPlain(plain);
            run.append(text.substr(start, offset - start));
        } else {
            flushRun(run);
            plain.append(text.substr(start, offset - start));
        }
    }
    flushRun(run);
    flushPlain(plain);

    std::string out;
    for (size_t i = 0; i < segments.size(); ++i) {
        if (i)
            out += " AND ";
        out += segments[i];
    }
    return out.empty() ? text : out;
}

} // namespace PersonOS::Infrastructure::Cjk
