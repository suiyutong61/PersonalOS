#include "infrastructure/embedding/SentencePieceUnigram.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace PersonOS::Infrastructure {

namespace {
// Unigram 平滑参数(与 SentencePiece 默认接近,足够本地使用)
constexpr double kUnkPenalty = 10.0;
constexpr double kMinScore = -50.0;
} // namespace

bool SentencePieceUnigram::loadFromTokenizerJson(const QByteArray &json)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;
    const QJsonArray vocab = document.object()
                                 .value(QStringLiteral("model"))
                                 .toObject()
                                 .value(QStringLiteral("vocab"))
                                 .toArray();
    if (vocab.isEmpty())
        return false;

    m_vocab.clear();
    m_tokenToId.clear();
    m_trie.clear();
    m_trie.append(Node{});   // 根节点

    for (int id = 0; id < vocab.size(); ++id) {
        const QJsonArray pair = vocab.at(id).toArray();
        if (pair.size() < 2)
            continue;
        const QString token = pair.at(0).toString();
        const double score = pair.at(1).toDouble();
        m_vocab.append({token, score});
        m_tokenToId.insert(token, id);
        // 插入字典树
        int node = 0;
        for (const QChar c : token) {
            const auto child = m_trie[node].children.constFind(c);
            if (child == m_trie[node].children.constEnd()) {
                m_trie[node].children.insert(c, m_trie.size());
                node = m_trie.size();
                m_trie.append(Node{});
            } else {
                node = child.value();
            }
        }
        m_trie[node].tokenId = id;
        m_trie[node].score = score;
    }
    const auto unk = m_tokenToId.constFind(QStringLiteral("<unk>"));
    if (unk != m_tokenToId.constEnd())
        m_unkScore = m_vocab.at(unk.value()).second - kUnkPenalty;
    return true;
}

std::vector<int> SentencePieceUnigram::encode(const QString &text) const
{
    std::vector<int> result;
    if (!loaded() || text.isEmpty())
        return result;

    // XLM-R 词表用 U+2581 表示词边界空格;普通空格须先转换,
    // 否则空格全部落入字节回退,词边界信息丢失导致语义塌缩。
    QString normalized = text;
    normalized.replace(QLatin1Char(' '), QChar(0x2581));

    // 文本 -> 码点序列(UTF-16 代理对合并)
    QVector<uint32_t> codePoints;
    codePoints.reserve(normalized.size());
    for (int i = 0; i < normalized.size(); ++i) {
        const uint32_t cp = normalized.at(i).unicode();
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < normalized.size()) {
            const uint32_t low = normalized.at(i + 1).unicode();
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codePoints.append(0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00));
                ++i;
                continue;
            }
        }
        codePoints.append(cp);
    }
    const int n = codePoints.size();

    // Viterbi:best[i] = (最大对数分数, 回溯tokenId 或 -1=无匹配)
    QVector<double> best(n + 1, -1e18);
    QVector<int> back(n + 1, -1);
    best[0] = 0.0;

    // 预处理每个位置沿 trie 的所有词(词表最长 token 有限,直接逐位匹配)
    const int maxTokenLength = 128;
    for (int i = 0; i < n; ++i) {
        if (best[i] < -1e17)
            continue;
        int node = 0;
        for (int j = i; j < n && j - i < maxTokenLength; ++j) {
            const auto child = m_trie[node].children.constFind(QChar(codePoints.at(j)));
            if (child == m_trie[node].children.constEnd())
                break;
            node = child.value();
            if (m_trie[node].tokenId >= 0) {
                const double candidate = best[i] + m_trie[node].score;
                if (candidate > best[j + 1]) {
                    best[j + 1] = candidate;
                    back[j + 1] = m_trie[node].tokenId;
                }
            }
        }
        // 无匹配时:按字节回退(每个 UTF-8 字节一个 <0xXX> token);
        // 仅当没有任何以 i 开头的 token 命中时使用
        if (back[i + 1] < 0) {
            const double candidate = best[i] + m_unkScore;
            if (candidate > best[i + 1]) {
                best[i + 1] = candidate;
                back[i + 1] = -2;   // 标记字节回退
            }
        }
    }

    // 回溯(优先最长/最少的字节回退)
    QVector<int> reversed;
    int pos = n;
    while (pos > 0) {
        const int tokenId = back[pos];
        if (tokenId >= 0) {
            reversed.append(tokenId);
            const QString &token = m_vocab.at(tokenId).first;
            // 按码点数回退
            int length = 0;
            for (const QChar c : token)
                if (!(c.unicode() >= 0xDC00 && c.unicode() <= 0xDFFF))
                    ++length;
            pos -= std::max(1, length);
        } else {
            // 字节回退:输出该码点的 UTF-8 字节 token
            uint32_t cp = codePoints.at(pos - 1);
            char bytes[4];
            int count = 0;
            if (cp < 0x80)
                bytes[count++] = static_cast<char>(cp);
            else if (cp < 0x800) {
                bytes[count++] = static_cast<char>(0xC0 | (cp >> 6));
                bytes[count++] = static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                bytes[count++] = static_cast<char>(0xE0 | (cp >> 12));
                bytes[count++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                bytes[count++] = static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                bytes[count++] = static_cast<char>(0xF0 | (cp >> 18));
                bytes[count++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                bytes[count++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                bytes[count++] = static_cast<char>(0x80 | (cp & 0x3F));
            }
            for (int b = 0; b < count; ++b) {
                const QString byteToken =
                    QStringLiteral("<0x%1>")
                        .arg(static_cast<unsigned char>(bytes[b]), 2, 16, QLatin1Char('0'));
                const auto found = m_tokenToId.constFind(byteToken);
                if (found != m_tokenToId.constEnd())
                    reversed.append(found.value());
                else
                    reversed.append(3);   // <unk>
            }
            --pos;
        }
    }
    std::reverse(reversed.begin(), reversed.end());
    for (const int id : reversed)
        result.push_back(id);
    return result;
}

} // namespace PersonOS::Infrastructure
