#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include <string>
#include <vector>

// SentencePiece Unigram 分词器(纯 C++,零外部依赖):
// 从 HF fast tokenizer.json 读取 XLM-R 词表(token→score),
// 以字典树 + Viterbi 求解最优切分;词表外字符按字节回退
// (XLM-R 词表含 <0x00>..<0xFF> 字节 token)。
// 用于本地向量化的 multilingual-e5-small 模型。
namespace PersonOS::Infrastructure {

class SentencePieceUnigram
{
public:
    // 从 tokenizer.json 的完整文件内容加载;失败返回 false
    bool loadFromTokenizerJson(const QByteArray &json);

    // 编码文本为 token id 序列(不含特殊 token;由调用方加 <s>/</s>)
    std::vector<int> encode(const QString &text) const;

    bool loaded() const { return !m_vocab.empty(); }
    int vocabSize() const { return static_cast<int>(m_vocab.size()); }
    int padId() const { return 1; }

private:
    struct Node
    {
        int tokenId = -1;
        double score = 0.0;
        QHash<QChar, int> children;
    };

    QVector<Node> m_trie;
    QVector<QPair<QString, double>> m_vocab;   // tokenId -> (token, score)
    QHash<QString, int> m_tokenToId;
    double m_unkScore = -10.0;
    double m_defaultScore = 0.0;
};

} // namespace PersonOS::Infrastructure
