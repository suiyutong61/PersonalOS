#include <QtTest>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

#include "infrastructure/embedding/DomainClassifier.h"
#include "infrastructure/embedding/LocalEmbeddingProvider.h"
#include "infrastructure/embedding/SentencePieceUnigram.h"

// 本地向量化验证(真实模型 multilingual-e5-small int8):
// 分词器健康度、Provider 语义相似度、43 领域分类推荐。
// 依赖 PERSONOS_EMBEDDING_DIR 下的 model-q8.onnx 与 tokenizer.json;
// 缺失时跳过。

namespace {

QString embeddingDir()
{
    QString dir = qEnvironmentVariable("PERSONOS_EMBEDDING_DIR");
    if (dir.isEmpty())
        dir = QStringLiteral("D:/PersonOS-All-Task/libs/models/minilm");
    return dir;
}

double cosine(const std::vector<float> &a, const std::vector<float> &b)
{
    if (a.empty() || b.empty())
        return -2.0;
    double dot = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        dot += static_cast<double>(a[i]) * b[i];
    return dot;
}

} // namespace

class TstLocalEmbedding : public QObject
{
    Q_OBJECT

private slots:
    void tokenizerProducesHealthyTokens()
    {
        const QString tokenizerPath = embeddingDir() + QStringLiteral("/tokenizer.json");
        if (!QFile::exists(tokenizerPath))
            QSKIP("tokenizer.json not present");
        QFile file(tokenizerPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        PersonOS::Infrastructure::SentencePieceUnigram tokenizer;
        QVERIFY2(tokenizer.loadFromTokenizerJson(file.readAll()), "load failed");
        QVERIFY(tokenizer.vocabSize() > 200000);

        // 常见中英文必须零字节回退
        const auto tokens = tokenizer.encode(QStringLiteral("query: 高质量睡眠"));
        QVERIFY(!tokens.empty());
        QCOMPARE(std::count(tokens.begin(), tokens.end(), 3), 0);   // 无 <unk>
    }

    void providerSimilarityMatchesReference()
    {
        const QString dir = embeddingDir();
        const QString modelPath = dir + QStringLiteral("/model-q8.onnx");
        const QString tokenizerPath = dir + QStringLiteral("/tokenizer.json");
        if (!QFile::exists(modelPath) || !QFile::exists(tokenizerPath))
            QSKIP("local embedding model not present");

        PersonOS::Infrastructure::LocalEmbeddingProvider provider(modelPath, tokenizerPath);
        QVERIFY2(provider.isReady(), qPrintable(provider.lastError()));
        QCOMPARE(provider.dimension(), 384);

        const auto a = provider.embedQuery("高质量睡眠能提高学习效率");
        const auto b = provider.embedQuery("睡眠不足会损害记忆力");
        const auto c = provider.embedQuery("杠杆收购的税务筹划方法");
        const auto d = provider.embedQuery("Good sleep improves learning outcomes");
        const auto a2 = provider.embedQuery("高质量睡眠能提高学习效率");
        QVERIFY(a.hasValue() && b.hasValue() && c.hasValue() && d.hasValue() && a2.hasValue());

        const double identical = cosine(a.value(), a2.value());
        const double sleepPair = cosine(a.value(), b.value());
        const double unrelated = cosine(a.value(), c.value());
        const double crossLingual = cosine(a.value(), d.value());
        qInfo() << "identical:" << identical << "| sleep pair:" << sleepPair
                << "| unrelated:" << unrelated << "| cross-lingual:" << crossLingual;
        QVERIFY(identical > 0.999);
        QVERIFY(sleepPair > 0.85);
        QVERIFY(sleepPair > unrelated);
        QVERIFY(crossLingual > 0.85);
    }

    void longTextTruncatesAndNormalizes()
    {
        const QString dir = embeddingDir();
        const QString modelPath = dir + QStringLiteral("/model-q8.onnx");
        const QString tokenizerPath = dir + QStringLiteral("/tokenizer.json");
        if (!QFile::exists(modelPath) || !QFile::exists(tokenizerPath))
            QSKIP("local embedding model not present");

        PersonOS::Infrastructure::LocalEmbeddingProvider provider(modelPath, tokenizerPath);
        QVERIFY2(provider.isReady(), qPrintable(provider.lastError()));

        // 远超 e5 512-token 上限的输入：截断后仍成功、384 维、L2 归一化
        QString longText;
        for (int i = 0; i < 300; ++i)
            longText += QStringLiteral("高效学习需要充足睡眠与规律复习。");
        const auto embedded = provider.embedDocument(longText.toStdString());
        QVERIFY2(embedded.hasValue(), qPrintable(provider.lastError()));
        QCOMPARE(embedded.value().size(), size_t(384));
        double norm = 0.0;
        for (const float v : embedded.value())
            norm += static_cast<double>(v) * v;
        norm = std::sqrt(norm);
        QVERIFY(norm > 0.99 && norm < 1.01);
    }

    void embedDocumentMatchesEmbedPassage()
    {
        const QString dir = embeddingDir();
        const QString modelPath = dir + QStringLiteral("/model-q8.onnx");
        const QString tokenizerPath = dir + QStringLiteral("/tokenizer.json");
        if (!QFile::exists(modelPath) || !QFile::exists(tokenizerPath))
            QSKIP("local embedding model not present");

        PersonOS::Infrastructure::LocalEmbeddingProvider provider(modelPath, tokenizerPath);
        QVERIFY2(provider.isReady(), qPrintable(provider.lastError()));

        // embedDocument（索引侧）与 embedPassage 使用同一 passage 前缀
        const auto doc = provider.embedDocument("高质量睡眠能提高学习效率");
        const auto passage = provider.embedPassage("高质量睡眠能提高学习效率");
        QVERIFY(doc.hasValue() && passage.hasValue());
        QVERIFY(cosine(doc.value(), passage.value()) > 0.999);
    }

    void domainClassifierRanksCorrectly()
    {
        const QString dir = embeddingDir();
        const QString modelPath = dir + QStringLiteral("/model-q8.onnx");
        const QString tokenizerPath = dir + QStringLiteral("/tokenizer.json");
        if (!QFile::exists(modelPath) || !QFile::exists(tokenizerPath))
            QSKIP("local embedding model not present");
        // 领域描述资源(domains.json)位于构建目录 embedding/ 或源资源目录
        const QString candidates[] = {
            QCoreApplication::applicationDirPath() + QStringLiteral("/embedding/domains.json"),
            QStringLiteral("D:/PersonOS-All-Task/PersonOS/resources/domains.json")};
        bool found = false;
        for (const QString &path : candidates)
            if (QFile::exists(path))
                found = true;
        if (!found)
            QSKIP("domains.json not present");

        // 直接用提供者做 43 域排序(分类器默认路径为应用目录,测试环境显式构造)
        PersonOS::Infrastructure::LocalEmbeddingProvider provider(modelPath, tokenizerPath);
        QVERIFY2(provider.isReady(), qPrintable(provider.lastError()));
        QFile domainsFile(QFile::exists(candidates[0]) ? candidates[0] : candidates[1]);
        QVERIFY(domainsFile.open(QIODevice::ReadOnly));
        const QJsonDocument document = QJsonDocument::fromJson(domainsFile.readAll());
        QVERIFY(document.isArray());
        QCOMPARE(document.array().size(), 43);

        struct Domain
        {
            QString code;
            std::vector<float> vector;
        };
        QVector<Domain> domains;
        for (const auto &value : document.array()) {
            const QJsonObject object = value.toObject();
            const QString description =
                object.value(QStringLiteral("title")).toString() + QStringLiteral(" ")
                + object.value(QStringLiteral("keywords")).toString().left(600);
            const auto embedded = provider.embedPassage(description.toStdString());
            QVERIFY2(embedded.hasValue(), qPrintable(provider.lastError()));
            domains.append({object.value(QStringLiteral("code")).toString(), embedded.value()});
        }
        QCOMPARE(domains.size(), 43);

        const auto rank = [&](const QString &text) -> QString {
            const auto query = provider.embedQuery(text.toStdString());
            if (!query.hasValue())
                return {};
            QVector<QPair<QString, double>> scores;
            for (const auto &domain : domains)
                scores.append({domain.code, cosine(query.value(), domain.vector)});
            std::sort(scores.begin(), scores.end(),
                      [](const auto &x, const auto &y) { return x.second > y.second; });
            return scores.first().first;
        };
        QCOMPARE(rank(QStringLiteral("Goal Commitment and the Goal-Setting Process")),
                 QStringLiteral("01"));
        QCOMPARE(rank(QStringLiteral("Sleep quality and academic performance: "
                                     "a systematic review")),
                 QStringLiteral("06"));
        QCOMPARE(rank(QStringLiteral("Does monitoring goal progress promote goal attainment?")),
                 QStringLiteral("01"));
    }
};

QTEST_GUILESS_MAIN(TstLocalEmbedding)
#include "tst_local_embedding.moc"
