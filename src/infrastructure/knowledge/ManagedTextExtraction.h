#pragma once

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QXmlStreamReader>

#include <optional>
#include <string>

#include "infrastructure/knowledge/ReliablePdfTextExtractor.h"

// 受管原件文本提取(批量管道与产品单文献流程共用,DR-009:不维护两套规则)
// 按资产扩展名分派:PDF → Poppler/OCR;JSON → NCBI BioC;XML → Europe PMC;
// 其他 → 原始文本。
namespace PersonOS::Infrastructure {

inline std::string extractBioCText(const std::string &json)
{
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json));
    if (!document.isArray())
        return {};
    QStringList parts;
    for (const auto &collectionValue : document.array()) {
        for (const auto &documentValue
             : collectionValue.toObject().value(QStringLiteral("documents")).toArray()) {
            for (const auto &passageValue
                 : documentValue.toObject().value(QStringLiteral("passages")).toArray()) {
                const QJsonObject passage = passageValue.toObject();
                const QString section = passage.value(QStringLiteral("infons")).toObject()
                                            .value(QStringLiteral("section_type")).toString();
                if (section.compare(QStringLiteral("REF"), Qt::CaseInsensitive) == 0)
                    continue;
                const QString text = passage.value(QStringLiteral("text")).toString().trimmed();
                if (!text.isEmpty())
                    parts.append(text);
            }
        }
    }
    return parts.join(QStringLiteral("\n\n")).toStdString();
}

// Europe PMC 官方 fullTextXML 正文提取：只收 p/title/article-title/abstract
// 元素的文本，跳过 back/ref-list（参考文献）。
inline std::string extractEuropePmcXmlText(const std::string &xml)
{
    QXmlStreamReader reader(QByteArray::fromStdString(xml));
    QStringList parts;
    QString buffer;
    int backDepth = 0;
    int captureDepth = 0;
    const auto isBackMarker = [](QStringView name) {
        return name == QLatin1String("back") || name == QLatin1String("ref-list");
    };
    const auto isTextElement = [](QStringView name) {
        return name == QLatin1String("p") || name == QLatin1String("title")
               || name == QLatin1String("article-title")
               || name == QLatin1String("abstract");
    };
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::StartElement:
            if (isBackMarker(reader.name()))
                ++backDepth;
            else if (backDepth == 0 && isTextElement(reader.name()))
                ++captureDepth;
            break;
        case QXmlStreamReader::EndElement:
            if (isBackMarker(reader.name())) {
                if (backDepth > 0)
                    --backDepth;
                break;
            }
            if (captureDepth > 0 && isTextElement(reader.name())) {
                const QString text = buffer.simplified();
                if (!text.isEmpty())
                    parts.append(text);
                buffer.clear();
                --captureDepth;
            }
            break;
        case QXmlStreamReader::Characters:
            if (captureDepth > 0 && backDepth == 0)
                buffer += reader.text();
            break;
        default:
            break;
        }
    }
    return parts.join(QStringLiteral("\n\n")).toStdString();
}

// 读取受管原件并提取全文;PDF 路径要求已通过 applyPdfToolPaths 配置外部工具。
// 失败返回 nullopt(错误写入 errorOut)。
inline std::optional<std::string> extractManagedFullText(const QString &absolutePath,
                                                         std::string &errorOut)
{
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        errorOut = "managed asset unreadable";
        return std::nullopt;
    }
    const QByteArray bytes = file.readAll();
    file.close();
    const std::string raw(bytes.constData(), static_cast<size_t>(bytes.size()));
    if (absolutePath.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
        const auto extracted = ReliablePdfTextExtractor::extract(raw, &errorOut);
        if (!extracted)
            return std::nullopt;
        return extracted->text;
    }
    if (absolutePath.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        const std::string text = extractBioCText(raw);
        if (text.empty()) {
            errorOut = "BioC full text unavailable";
            return std::nullopt;
        }
        return text;
    }
    if (absolutePath.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive)) {
        const std::string text = extractEuropePmcXmlText(raw);
        if (text.empty()) {
            errorOut = "Europe PMC full text unavailable";
            return std::nullopt;
        }
        return text;
    }
    if (raw.empty()) {
        errorOut = "managed asset empty";
        return std::nullopt;
    }
    return raw;
}

} // namespace PersonOS::Infrastructure
