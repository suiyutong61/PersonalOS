#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace PersonOS::Infrastructure {

// AllPaper 的稳定中间结构。所有推断都保留原文件与行号，缺失字段以警告表达，
// 不用默认值伪装成索引事实。
struct IndexedPaper
{
    QString sourceFile;
    int startLine = 0;
    int endLine = 0;
    QString paperNumber;
    QString title;
    QString year;
    QString type;
    QStringList viewUrls;
    QStringList downloadUrls;
    QStringList researchFocus;
    QStringList warnings;
};

struct IndexedDomain
{
    QString sourceFile;
    int number = 0;
    QString code;
    QString name;
    QStringList declaredSubdomains;
    QVector<IndexedPaper> papers;
    QStringList warnings;
};

struct CorpusIndex
{
    QVector<IndexedDomain> domains;
    QStringList warnings;

    qsizetype paperCount() const;
};

class MarkdownCorpusIndex
{
public:
    static CorpusIndex parseDirectory(const QString &directoryPath);
    static IndexedDomain parseFile(const QString &filePath, const QString &markdown);
};

} // namespace PersonOS::Infrastructure
