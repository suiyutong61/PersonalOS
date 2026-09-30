#include <QtTest>

#include <QDir>
#include <QRegularExpression>

#include <algorithm>

#include "infrastructure/knowledge/MarkdownCorpusIndex.h"

using namespace PersonOS::Infrastructure;

class TstMarkdownCorpusIndex : public QObject
{
    Q_OBJECT

private slots:
    void parsesStandardAndMissingNumberCards()
    {
        const QString markdown = QString::fromUtf8(R"MD(# 43. Culture & Equity（文化与公平）

## 子领域
Culture / Language / Equity

## 43.1
- **编号**：43.1
- **文章名称**：First paper
- **时间**：2024
- **类型**：Systematic Review
- **查看网址**：[查看](https://example.org/article)
- **下载网址**：https://example.org/a.pdf https://mirror.example.org/a.pdf

## 43.11
- **文章名称**：Entry without explicit number
- **时间**：2023
- **类型**：Review
- **查看网址**：https://example.org/b
- **下载网址**：https://example.org/b.pdf
)MD");
        const auto domain = MarkdownCorpusIndex::parseFile(
            QStringLiteral("43-Culture-Equity.md"), markdown);
        QCOMPARE(domain.number, 43);
        QCOMPARE(domain.code, QStringLiteral("culture-equity"));
        QCOMPARE(domain.declaredSubdomains.size(), 3);
        QCOMPARE(domain.papers.size(), 2);
        QCOMPARE(domain.papers.at(0).downloadUrls.size(), 2);
        QCOMPARE(domain.papers.at(1).paperNumber, QStringLiteral("43.11"));
        QCOMPARE(domain.papers.at(1).title,
                 QStringLiteral("Entry without explicit number"));
    }

    void parsesLegacyEvidenceCard()
    {
        const QString markdown = QString::fromUtf8(R"MD(# 01. Goal & Self-Regulation

## 1. Locke & Latham (2002)

### Building a Useful Theory
- **Authors**: A. Author
- **Year**: 2002
- **Type**: Major Review
- **Research Focus**:
  - Goal Setting
  - Feedback
### Access
- **Original / PubMed**
  https://example.org/view
- **Open Full Text / PDF**
  https://example.org/full.pdf
)MD");
        const auto domain = MarkdownCorpusIndex::parseFile(
            QStringLiteral("01-Goal-Self-Regulation.md"), markdown);
        QCOMPARE(domain.papers.size(), 1);
        QCOMPARE(domain.papers.first().title, QStringLiteral("Building a Useful Theory"));
        QCOMPARE(domain.papers.first().year, QStringLiteral("2002"));
        QCOMPARE(domain.papers.first().viewUrls.size(), 1);
        QCOMPARE(domain.papers.first().downloadUrls.size(), 1);
        QCOMPARE(domain.papers.first().researchFocus.size(), 2);
    }

    void parsesH3NumberedAppendBatch()
    {
        const QString markdown = QString::fromUtf8(R"MD(# 04. Learning Science

## 4.10
- **编号**：4.10
- **文章名称**：Tenth paper
- **时间**：2024
- **类型**：Review
- **查看网址**：https://example.org/10

### 4.11
- **编号**：4.11
- **文章名称**：Eleventh paper
- **时间**：2023
- **类型**：Review
- **查看网址**：https://example.org/11

### 4.12
- **编号**：4.12
- **文章名称**：Twelfth paper
- **时间**：2022
- **类型**：Review
- **查看网址**：https://example.org/12
)MD");
        const auto domain = MarkdownCorpusIndex::parseFile(
            QStringLiteral("04-Learning-Science.md"), markdown);
        QCOMPARE(domain.papers.size(), 3);
        QCOMPARE(domain.papers.at(0).paperNumber, QStringLiteral("4.10"));
        QCOMPARE(domain.papers.at(1).paperNumber, QStringLiteral("4.11"));
        QCOMPARE(domain.papers.at(2).paperNumber, QStringLiteral("4.12"));
        QCOMPARE(domain.papers.at(0).title, QStringLiteral("Tenth paper"));
        QCOMPARE(domain.papers.at(1).title, QStringLiteral("Eleventh paper"));
        QCOMPARE(domain.papers.at(2).title, QStringLiteral("Twelfth paper"));
    }

    void ignoresH3NumberedSubdomainHeadings()
    {
        const QString markdown = QString::fromUtf8(R"MD(# 07. Nutrition

## 子领域
### 7.1 Macronutrients
Protein / Carbohydrates / Fat

### 7.2 Protein
Sources / Intake

## 7.1
- **编号**：7.1
- **文章名称**：First real paper
- **时间**：2024
- **类型**：Review
- **查看网址**：https://example.org/paper
)MD");
        const auto domain = MarkdownCorpusIndex::parseFile(
            QStringLiteral("07-Nutrition.md"), markdown);
        QCOMPARE(domain.papers.size(), 1);
        QCOMPARE(domain.papers.first().paperNumber, QStringLiteral("7.1"));
        QCOMPARE(domain.papers.first().title, QStringLiteral("First real paper"));
    }

    void extractsUrlLabeledLinksWithoutGarbage()
    {
        const QString markdown = QString::fromUtf8(R"MD(# 08. Exercise

## 8.1
- **编号**：8.1
- **文章名称**：Link label paper
- **时间**：2024
- **类型**：Review
- **查看网址**：[https://pmc.ncbi.nlm.nih.gov/articles/PMC10040444/](https://pmc.ncbi.nlm.nih.gov/articles/PMC10040444/?utm_source=chatgpt.com)
- **下载网址**：https://example.org/x.pdf
)MD");
        const auto domain = MarkdownCorpusIndex::parseFile(
            QStringLiteral("08-Exercise.md"), markdown);
        QCOMPARE(domain.papers.size(), 1);
        const auto &paper = domain.papers.first();
        QCOMPARE(paper.viewUrls.size(), 1);
        QCOMPARE(paper.viewUrls.first(),
                 QStringLiteral("https://pmc.ncbi.nlm.nih.gov/articles/PMC10040444/"
                                "?utm_source=chatgpt.com"));
        for (const auto &url : paper.viewUrls)
            QVERIFY2(!url.contains(QStringLiteral("](")), qPrintable(url));
    }

    void extractsLinksWithSpacesInHref()
    {
        const QString markdown = QString::fromUtf8(R"MD(# 36. Life Transitions

## 36.1
- **编号**：36.1
- **文章名称**：Spaced href paper
- **时间**：2024
- **类型**：Review
- **查看网址**：[https://example.org/Holliman_Accepted Ball%2C2024.pdf](https://example.org/Holliman_Accepted%20Ball%2C2024.pdf)
)MD");
        const auto domain = MarkdownCorpusIndex::parseFile(
            QStringLiteral("36-Life-Transitions.md"), markdown);
        QCOMPARE(domain.papers.size(), 1);
        const auto &paper = domain.papers.first();
        QCOMPARE(paper.viewUrls.size(), 1);
        QCOMPARE(paper.viewUrls.first(),
                 QStringLiteral("https://example.org/Holliman_Accepted%20Ball%2C2024.pdf"));
    }

    void fallsBackToEncodedLabelWhenHrefHasSpaces()
    {
        const QString markdown = QString::fromUtf8(R"MD(# 36. Life Transitions

## 36.1
- **编号**：36.1
- **文章名称**：Messy href paper
- **时间**：2024
- **类型**：Review
- **下载网址**：[https://discovery.ucl.ac.uk/id/eprint/10192961/1/Holliman_Accepted%20Ball%2C%20Banerjee%2C%20Holliman%20%26%20Tyndall%20%282024%29%20Academic%20Achievement%20SR%20EPR%5B262808%5D.pdf](https://discovery.ucl.ac.uk/id/eprint/10192961/1/Holliman_Accepted Ball%2C Banerjee%2C Holliman %26 Tyndall (2024) Academic Achievement SR EPR[262808].pdf)
)MD");
        const auto domain = MarkdownCorpusIndex::parseFile(
            QStringLiteral("36-Life-Transitions.md"), markdown);
        QCOMPARE(domain.papers.size(), 1);
        const auto &paper = domain.papers.first();
        QCOMPARE(paper.downloadUrls.size(), 1);
        QCOMPARE(paper.downloadUrls.first(),
                 QStringLiteral("https://discovery.ucl.ac.uk/id/eprint/10192961/1/"
                                "Holliman_Accepted%20Ball%2C%20Banerjee%2C%20Holliman"
                                "%20%26%20Tyndall%20%282024%29%20Academic%20Achievement"
                                "%20SR%20EPR%5B262808%5D.pdf"));
    }

    void warnsOnDuplicatePaperNumbers()
    {
        const QString markdown = QString::fromUtf8(R"MD(# 07. Nutrition

## 7.1
- **编号**：7.1
- **文章名称**：First set paper one
- **时间**：2024
- **类型**：Review
- **查看网址**：https://example.org/a

## 7.1
- **编号**：7.1
- **文章名称**：Second set paper one
- **时间**：2023
- **类型**：Review
- **查看网址**：https://example.org/b
)MD");
        const auto domain = MarkdownCorpusIndex::parseFile(
            QStringLiteral("07-Nutrition.md"), markdown);
        QCOMPARE(domain.papers.size(), 2);
        QCOMPARE(domain.warnings.size(), 1);
        QVERIFY2(domain.warnings.first().contains(QStringLiteral("duplicate paper number")),
                 qPrintable(domain.warnings.first()));
        QVERIFY2(domain.warnings.first().contains(QStringLiteral("7.1")),
                 qPrintable(domain.warnings.first()));
    }

    void parsesRealCorpusWhenAvailable()
    {
        const QString path = qEnvironmentVariable("PERSONOS_ALLPAPER_DIR");
        if (path.isEmpty() || !QDir(path).exists())
            QSKIP("PERSONOS_ALLPAPER_DIR not configured");
        const auto index = MarkdownCorpusIndex::parseDirectory(path);
        QCOMPARE(index.domains.size(), 43);
        QCOMPARE(index.paperCount(), 938);
        // 07 号文件原有两套 7.1–7.10 重复编号（内容不同），已将第二套改编号
        // 为 7.41–7.50：全库应零警告，域 7 编号连续。
        QVERIFY2(index.warnings.isEmpty(), qPrintable(index.warnings.join('\n')));
        for (const auto &domain : index.domains) {
            QVERIFY(domain.number >= 1 && domain.number <= 43);
            QVERIFY(!domain.code.isEmpty());
            QVERIFY(!domain.papers.isEmpty());
            for (const auto &paper : domain.papers) {
                QVERIFY(!paper.paperNumber.isEmpty());
                QVERIFY(!paper.title.isEmpty());
                for (const auto &urls : {paper.viewUrls, paper.downloadUrls})
                    for (const auto &url : urls)
                        QVERIFY2((url.startsWith(QStringLiteral("https://"))
                                  || url.startsWith(QStringLiteral("http://")))
                                     && !url.contains(QStringLiteral("]("))
                                     && !url.contains(QStringLiteral("("))
                                     && !url.contains(QStringLiteral(")"))
                                     && !url.contains(QRegularExpression(QStringLiteral("\\s"))),
                                 qPrintable(url));
            }
        }
        // 03/04 的 ### NN.M 追加批次编号必须连续且不再错位
        const auto domain03 = std::find_if(index.domains.cbegin(), index.domains.cend(),
                                           [](const auto &d) { return d.number == 3; });
        const auto domain04 = std::find_if(index.domains.cbegin(), index.domains.cend(),
                                           [](const auto &d) { return d.number == 4; });
        QVERIFY(domain03 != index.domains.cend());
        QVERIFY(domain04 != index.domains.cend());
        QCOMPARE(domain03->papers.size(), 18);
        QCOMPARE(domain04->papers.size(), 26);
        for (int i = 0; i < domain03->papers.size(); ++i)
            QCOMPARE(domain03->papers.at(i).paperNumber, QStringLiteral("3.%1").arg(i + 1));
        for (int i = 0; i < domain04->papers.size(); ++i)
            QCOMPARE(domain04->papers.at(i).paperNumber, QStringLiteral("4.%1").arg(i + 1));
        // 07 号第二套改编号后:50 条、编号集合恰好为 7.1–7.50 且无重复
        // (文件中第二套位于第三套之前,故断言编号集合而非文件序连续)
        const auto domain07 = std::find_if(index.domains.cbegin(), index.domains.cend(),
                                           [](const auto &d) { return d.number == 7; });
        QVERIFY(domain07 != index.domains.cend());
        QCOMPARE(domain07->papers.size(), 50);
        QStringList numbers07;
        for (const auto &paper : domain07->papers)
            numbers07.append(paper.paperNumber);
        std::sort(numbers07.begin(), numbers07.end(),
                  [](const QString &a, const QString &b) {
                      return a.mid(2).toInt() < b.mid(2).toInt();
                  });
        for (int i = 0; i < numbers07.size(); ++i)
            QCOMPARE(numbers07.at(i), QStringLiteral("7.%1").arg(i + 1));
    }
};

QTEST_GUILESS_MAIN(TstMarkdownCorpusIndex)
#include "tst_markdown_corpus_index.moc"
