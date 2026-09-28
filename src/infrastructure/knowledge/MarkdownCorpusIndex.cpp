#include "infrastructure/knowledge/MarkdownCorpusIndex.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace PersonOS::Infrastructure {

namespace {

QString cleanInline(QString value)
{
    value = value.trimmed();
    value.remove(QRegularExpression(QStringLiteral("^[-*]\\s*")));
    value.remove(QStringLiteral("**"));
    // 去掉字段值内嵌的 markdown 链接语法（链接本身由 urlsIn 单独提取）
    value.remove(QRegularExpression(QStringLiteral("\\[[^\\]]*\\]\\([^)]*\\)")));
    return value.trimmed();
}

QString fieldValue(const QString &line, const QStringList &labels)
{
    for (const QString &label : labels) {
        const QRegularExpression expression(
            QStringLiteral("^\\s*[-*]?\\s*(?:\\*\\*)?%1(?:\\*\\*)?\\s*[:：]\\s*(.*)$")
                .arg(QRegularExpression::escape(label)),
            QRegularExpression::CaseInsensitiveOption);
        const auto match = expression.match(line);
        if (match.hasMatch())
            return cleanInline(match.captured(1));
    }
    return {};
}

QStringList urlsIn(const QString &line)
{
    QStringList urls;
    QSet<QString> seen;
    static const QRegularExpression markdownLink(
        QStringLiteral("\\[([^\\]]*)\\]\\((https?://[^)]*)\\)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bareUrl(
        QStringLiteral("https?://[^\\s)>\\]\\[({}]+"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression whitespace(QStringLiteral("\\s"));

    const auto stripTrailing = [](QString url) {
        while (url.endsWith(QLatin1Char('.')) || url.endsWith(QLatin1Char(',')))
            url.chop(1);
        return url;
    };
    const auto append = [&](const QString &url) {
        if (!url.isEmpty() && !seen.contains(url)) {
            seen.insert(url);
            urls.append(url);
        }
    };

    // markdown 链接:记录匹配区间,裸链接扫描时跳过,避免把标签里的 URL
    // 与 href 拼成垃圾地址。真实数据（36 号文件）存在 href 含字面空格/裸括号
    // 的链接,此时回退到标签内的百分号编码 URL。
    QList<QPair<int, int>> linkSpans;
    auto links = markdownLink.globalMatch(line);
    while (links.hasNext()) {
        const auto link = links.next();
        linkSpans.append({link.capturedStart(0), link.capturedEnd(0)});
        QString href = stripTrailing(link.captured(2).trimmed());
        if (href.contains(whitespace)) {
            QString fallback;
            auto labelUrls = bareUrl.globalMatch(link.captured(1));
            while (labelUrls.hasNext()) {
                const QString candidate = stripTrailing(labelUrls.next().captured(0));
                if (!candidate.contains(whitespace)) {
                    fallback = candidate;
                    break;
                }
            }
            append(fallback);
        } else {
            append(href);
        }
    }
    auto bare = bareUrl.globalMatch(line);
    while (bare.hasNext()) {
        const auto match = bare.next();
        const int start = match.capturedStart(0);
        bool insideLink = false;
        for (const auto &span : linkSpans)
            if (start >= span.first && start < span.second) {
                insideLink = true;
                break;
            }
        if (insideLink)
            continue;
        append(stripTrailing(match.captured(0)));
    }
    return urls;
}

void appendUnique(QStringList &target, const QStringList &values)
{
    for (const QString &value : values)
        if (!value.isEmpty() && !target.contains(value))
            target.append(value);
}

QString slugFromFile(const QString &filePath)
{
    QString slug = QFileInfo(filePath).completeBaseName();
    slug.remove(QRegularExpression(QStringLiteral("^\\d{2}-")));
    slug = slug.toLower();
    slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    return slug.remove(QRegularExpression(QStringLiteral("(^-+|-+$)")));
}

QStringList splitFocus(const QString &text)
{
    QStringList result;
    for (QString part : text.split(QRegularExpression(QStringLiteral("[/,;|]")),
                                   Qt::SkipEmptyParts)) {
        part = cleanInline(part);
        if (!part.isEmpty())
            result.append(part);
    }
    return result;
}

} // namespace

qsizetype CorpusIndex::paperCount() const
{
    qsizetype count = 0;
    for (const auto &domain : domains)
        count += domain.papers.size();
    return count;
}

CorpusIndex MarkdownCorpusIndex::parseDirectory(const QString &directoryPath)
{
    CorpusIndex index;
    QDir directory(directoryPath);
    if (!directory.exists()) {
        index.warnings.append(QStringLiteral("index directory does not exist: %1")
                                  .arg(directoryPath));
        return index;
    }

    const QStringList files = directory.entryList(
        {QStringLiteral("[0-9][0-9]-*.md")}, QDir::Files, QDir::Name);
    QSet<int> numbers;
    for (const QString &name : files) {
        QFile file(directory.absoluteFilePath(name));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            index.warnings.append(QStringLiteral("cannot read index file: %1").arg(name));
            continue;
        }
        IndexedDomain domain = parseFile(file.fileName(), QString::fromUtf8(file.readAll()));
        if (numbers.contains(domain.number))
            index.warnings.append(QStringLiteral("duplicate domain number: %1").arg(domain.number));
        numbers.insert(domain.number);
        for (const auto &warning : domain.warnings)
            index.warnings.append(warning);
        index.domains.append(std::move(domain));
    }
    for (int expected = 1; expected <= 43; ++expected)
        if (!numbers.contains(expected))
            index.warnings.append(QStringLiteral("missing domain number: %1").arg(expected));
    return index;
}

IndexedDomain MarkdownCorpusIndex::parseFile(const QString &filePath, const QString &markdown)
{
    IndexedDomain domain;
    domain.sourceFile = filePath;
    const QFileInfo info(filePath);
    const auto numberMatch = QRegularExpression(QStringLiteral("^(\\d{2})-"))
                                 .match(info.fileName());
    if (numberMatch.hasMatch())
        domain.number = numberMatch.captured(1).toInt();
    else
        domain.warnings.append(QStringLiteral("domain number missing from filename"));
    domain.code = slugFromFile(filePath);

    const QStringList lines = markdown.split(QLatin1Char('\n'));
    static const QRegularExpression h1(QStringLiteral("^#\\s+(?:\\d{1,2}\\.\\s*)?(.+)$"));
    static const QRegularExpression paperHeading(
        QStringLiteral("^#{2,3}\\s+(\\d+(?:\\.\\d+)?)(?:\\.)?\\s*(.*)$"));
    static const QRegularExpression h3(QStringLiteral("^###\\s+(.+)$"));

    bool inSubdomains = false;
    int currentStart = -1;
    QString currentHeadingNumber;
    QString currentHeadingText;
    bool currentHasArticleField = false;

    const auto finishPaper = [&](int endExclusive, QVector<IndexedPaper> &papers) {
        if (currentStart < 0)
            return;
        IndexedPaper paper;
        paper.sourceFile = filePath;
        paper.startLine = currentStart + 1;
        paper.endLine = endExclusive;
        paper.paperNumber = currentHeadingNumber;
        QString headingTitle = currentHeadingText.trimmed();
        QString pendingUrlKind;
        bool inResearchFocus = false;

        for (int i = currentStart + 1; i < endExclusive; ++i) {
            const QString line = lines.at(i).trimmed();
            QString value;
            if (!(value = fieldValue(line, {QStringLiteral("编号")})).isEmpty())
                paper.paperNumber = value;
            else if (!(value = fieldValue(line, {QStringLiteral("文章名称"),
                                                   QStringLiteral("论文名称"),
                                                   QStringLiteral("Title")})).isEmpty())
                paper.title = value;
            else if (!(value = fieldValue(line, {QStringLiteral("时间"),
                                                   QStringLiteral("年份"),
                                                   QStringLiteral("Year")})).isEmpty())
                paper.year = value;
            else if (!(value = fieldValue(line, {QStringLiteral("类型"),
                                                   QStringLiteral("Type")})).isEmpty())
                paper.type = value;

            if (line.contains(QStringLiteral("查看网址"))) {
                appendUnique(paper.viewUrls, urlsIn(line));
                pendingUrlKind = QStringLiteral("view");
            } else if (line.contains(QStringLiteral("下载网址"))) {
                appendUnique(paper.downloadUrls, urlsIn(line));
                pendingUrlKind = QStringLiteral("download");
            } else if (line.contains(QStringLiteral("Open Full Text"), Qt::CaseInsensitive)
                       || line.contains(QStringLiteral("PDF"), Qt::CaseInsensitive)) {
                pendingUrlKind = QStringLiteral("download");
                appendUnique(paper.downloadUrls, urlsIn(line));
            } else if (line.contains(QStringLiteral("Original"), Qt::CaseInsensitive)
                       || line.contains(QStringLiteral("PubMed"), Qt::CaseInsensitive)
                       || line.contains(QStringLiteral("DOI"), Qt::CaseInsensitive)) {
                pendingUrlKind = QStringLiteral("view");
                appendUnique(paper.viewUrls, urlsIn(line));
            } else if (!urlsIn(line).isEmpty()) {
                if (pendingUrlKind == QStringLiteral("download"))
                    appendUnique(paper.downloadUrls, urlsIn(line));
                else
                    appendUnique(paper.viewUrls, urlsIn(line));
            }

            if (line.contains(QStringLiteral("Research Focus"), Qt::CaseInsensitive)) {
                inResearchFocus = true;
                appendUnique(paper.researchFocus, splitFocus(fieldValue(
                    line, {QStringLiteral("Research Focus")})));
                continue;
            }
            if (inResearchFocus) {
                if (line.startsWith(QStringLiteral("- **")) || line.startsWith(QStringLiteral("###")))
                    inResearchFocus = false;
                else if (line.startsWith(QLatin1Char('-')))
                    appendUnique(paper.researchFocus, splitFocus(line.mid(1)));
            }

            const auto titleMatch = h3.match(line);
            if (paper.title.isEmpty() && titleMatch.hasMatch()
                && titleMatch.captured(1).compare(QStringLiteral("Access"),
                                                  Qt::CaseInsensitive) != 0)
                paper.title = cleanInline(titleMatch.captured(1));
        }
        if (paper.title.isEmpty() && !headingTitle.isEmpty())
            paper.title = headingTitle;
        if (paper.title.isEmpty())
            paper.warnings.append(QStringLiteral("title missing"));
        if (paper.year.isEmpty())
            paper.warnings.append(QStringLiteral("year missing"));
        if (paper.type.isEmpty())
            paper.warnings.append(QStringLiteral("type missing"));
        if (paper.viewUrls.isEmpty() && paper.downloadUrls.isEmpty())
            paper.warnings.append(QStringLiteral("no usable URL"));
        papers.append(std::move(paper));
    };

    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (domain.name.isEmpty()) {
            const auto match = h1.match(line);
            if (match.hasMatch())
                domain.name = match.captured(1).trimmed();
        }
        if (line == QStringLiteral("## 子领域")) {
            inSubdomains = true;
            continue;
        }
        if (inSubdomains && line.startsWith(QStringLiteral("## "))) {
            inSubdomains = false;
        } else if (inSubdomains && !line.isEmpty() && !line.startsWith(QLatin1Char('>'))
                   && !line.startsWith(QStringLiteral("###"))) {
            appendUnique(domain.declaredSubdomains, splitFocus(line));
        }

        // 03/04 的追加批次用 ### NN.M 作条目标题；但 04/07/08 的子领域节内
        // 也有 ### N 小标题（如 "### 7.1 Macronutrients"），必须限定在子领域节外。
        const auto match = paperHeading.match(line);
        if (!inSubdomains && match.hasMatch()) {
            finishPaper(i, domain.papers);
            currentStart = i;
            currentHeadingNumber = match.captured(1);
            currentHeadingText = match.captured(2);
            currentHasArticleField = false;
            continue;
        }

        // 最后兜底：完全没有 ##/### 编号标题的条目仍以文章名称字段开头时，
        // 遇到同一块中的第二个文章名称，将其视为新条目边界并按顺序补出候选编号。
        if (!fieldValue(line, {QStringLiteral("文章名称"),
                               QStringLiteral("论文名称")}).isEmpty()) {
            if (currentStart >= 0 && currentHasArticleField) {
                finishPaper(i, domain.papers);
                currentStart = i;
                currentHeadingNumber = QStringLiteral("%1.%2")
                                           .arg(domain.number)
                                           .arg(domain.papers.size() + 1);
                currentHeadingText.clear();
            }
            currentHasArticleField = true;
        }
    }
    finishPaper(lines.size(), domain.papers);
    QSet<QString> seenNumbers;
    for (const auto &paper : domain.papers) {
        if (!paper.paperNumber.isEmpty() && seenNumbers.contains(paper.paperNumber))
            domain.warnings.append(QStringLiteral("duplicate paper number in domain %1: %2")
                                       .arg(domain.number).arg(paper.paperNumber));
        else
            seenNumbers.insert(paper.paperNumber);
    }
    if (domain.papers.isEmpty())
        domain.warnings.append(QStringLiteral("no paper entries parsed"));
    return domain;
}

} // namespace PersonOS::Infrastructure
