#include "infrastructure/embedding/DomainClassifier.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>

#include <algorithm>

#include "infrastructure/embedding/LocalEmbeddingProvider.h"
#include "infrastructure/knowledge/SqlEmbeddingRepository.h"
#include "infrastructure/persistence/DatabaseConnectionFactory.h"

namespace PersonOS::Infrastructure {

QString DomainClassifier::s_databasePath;

DomainClassifier &DomainClassifier::instance()
{
    static DomainClassifier classifier;
    return classifier;
}

DomainClassifier::DomainClassifier()
{
    m_provider = &sharedLocalEmbedding();
    // 仅检查文件存在(启动期不加载模型);实际加载在首次 suggest 时
    m_available = loadDomainsJson() && LocalEmbeddingProvider::filesPresent();
}

void DomainClassifier::configureDatabasePath(const QString &databasePath)
{
    s_databasePath = databasePath;
}

QString DomainClassifier::domainsJsonPath() const
{
    const QString appDir = QCoreApplication::applicationDirPath()
                           + QStringLiteral("/embedding/domains.json");
    if (QFile::exists(appDir))
        return appDir;
    return QStringLiteral(":/embedding/domains.json");
}

bool DomainClassifier::loadDomainsJson()
{
    QFile file(domainsJsonPath());
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = file.readAll();
    const QJsonDocument document = QJsonDocument::fromJson(raw);
    if (!document.isArray())
        return false;
    m_domainsHash = QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex();
    m_domains.clear();
    m_domainDescriptions.clear();
    for (const auto &value : document.array()) {
        const QJsonObject object = value.toObject();
        Candidate candidate;
        candidate.code = object.value(QStringLiteral("code")).toString();
        candidate.slug = object.value(QStringLiteral("slug")).toString();
        candidate.title = object.value(QStringLiteral("title")).toString();
        if (candidate.code.isEmpty() || candidate.slug.isEmpty())
            continue;
        m_domains.append(candidate);
        // keywords 存入 score 暂存字段?另存描述文本
        m_domains.last().score = 0.0;
        m_domainDescriptions.append(
            candidate.title + QStringLiteral(" ")
            + object.value(QStringLiteral("keywords")).toString().left(600));
    }
    return m_domains.size() == 43;
}

bool DomainClassifier::loadDomainVectorsFromDatabase(const QString &databasePath,
                                                     const std::string &modelId)
{
    DatabaseConnectionFactory factory(databasePath);
    QString error;
    const QSqlDatabase database =
        factory.openForCurrentThread(QStringLiteral("domain_seed"), &error);
    if (!database.isValid())
        return false;
    const bool loaded = [&]() {
        SqlEmbeddingRepository store(database);
        const auto rows = store.allForOwnerType("domain", modelId);
        if (rows.size() != 43)
            return false;
        // 逐域校验:内容指纹与当前 domains.json 一致且维度与提供者一致
        QVector<QVector<float>> vectors(m_domains.size());
        const QByteArray expectedHash = m_domainsHash;
        for (const auto &row : rows) {
            if (row.contentHash != expectedHash.toStdString()
                || row.dimension != m_provider->dimension())
                return false;
            int index = -1;
            for (int i = 0; i < m_domains.size(); ++i) {
                if (m_domains.at(i).code == QString::fromStdString(row.ownerUid)) {
                    index = i;
                    break;
                }
            }
            if (index < 0)
                return false;
            vectors[index] =
                QVector<float>(row.vector.begin(), row.vector.end());
        }
        for (const auto &vector : vectors)
            if (vector.isEmpty())
                return false;
        m_domainVectors = vectors;
        return true;
    }();
    DatabaseConnectionFactory::closeCurrentThreadConnection(QStringLiteral("domain_seed"));
    return loaded;
}

bool DomainClassifier::persistDomainVectorsToDatabase(const QString &databasePath,
                                                      const std::string &modelId)
{
    DatabaseConnectionFactory factory(databasePath);
    QString error;
    const QSqlDatabase database =
        factory.openForCurrentThread(QStringLiteral("domain_seed"), &error);
    if (!database.isValid())
        return false;
    bool ok = true;
    {
        SqlEmbeddingRepository store(database);
        for (int i = 0; i < m_domains.size(); ++i) {
            if (m_domainVectors.value(i).isEmpty())
                continue;
            Application::EmbeddingRecord record;
            record.ownerType = "domain";
            record.ownerUid = m_domains.at(i).code.toStdString();
            record.fieldCode = "description";
            record.modelId = modelId;
            record.dimension = m_provider->dimension();
            record.vector = {m_domainVectors.at(i).begin(), m_domainVectors.at(i).end()};
            const auto saved = store.upsert(record, m_domainsHash.toStdString());
            if (!saved.hasValue())
                ok = false;
        }
    }
    DatabaseConnectionFactory::closeCurrentThreadConnection(QStringLiteral("domain_seed"));
    return ok;
}

bool DomainClassifier::persistDomainsToDatabase(const QString &databasePath)
{
    DomainClassifier &classifier = instance();
    classifier.ensureSeeded();
    const std::string modelId =
        classifier.m_provider ? classifier.m_provider->modelId() : std::string();
    if (modelId.empty())
        return false;
    return classifier.persistDomainVectorsToDatabase(databasePath, modelId);
}

void DomainClassifier::ensureSeeded()
{
    QMutexLocker locker(&m_mutex);
    if (m_seeded)
        return;
    m_seeded = true;
    m_domainVectors.clear();

    const std::string modelId = m_provider ? m_provider->modelId() : std::string();
    // 数据库命中(43 行且指纹一致)→ 直接装载,免每次启动重算
    if (!s_databasePath.isEmpty() && !modelId.empty()
        && loadDomainVectorsFromDatabase(s_databasePath, modelId))
        return;

    // 计算 43 个领域向量(文档侧前缀);失败域记空向量,suggest 时跳过
    for (const QString &description : m_domainDescriptions) {
        const auto embedded = m_provider->embedDocument(description.toStdString());
        if (embedded)
            m_domainVectors.append(QVector<float>(embedded.value().begin(),
                                                   embedded.value().end()));
        else
            m_domainVectors.append(QVector<float>());
    }

    // 数据库可用时持久化(首次分类写入,后续启动直接装载)
    if (!s_databasePath.isEmpty() && !modelId.empty())
        persistDomainVectorsToDatabase(s_databasePath, modelId);
}

bool DomainClassifier::available() const
{
    return m_available;
}

QVector<DomainClassifier::Candidate> DomainClassifier::suggest(const QString &text, int topK)
{
    if (!m_available || !m_provider || text.trimmed().isEmpty())
        return {};
    ensureSeeded();
    const auto query = m_provider->embed(text.trimmed().toStdString());
    if (!query || query.value().empty())
        return {};
    const auto &q = query.value();
    QVector<Candidate> ranked;
    ranked.reserve(m_domains.size());
    for (int i = 0; i < m_domains.size(); ++i) {
        Candidate candidate = m_domains.at(i);
        if (m_domainVectors.value(i).empty())
            continue;
        double dot = 0.0;
        const auto &v = m_domainVectors.at(i);
        for (int d = 0; d < q.size(); ++d)
            dot += static_cast<double>(q[d]) * v[d];
        candidate.score = dot;
        ranked.append(candidate);
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
    return ranked.mid(0, topK);
}

void DomainClassifier::setProviderForTesting(Application::EmbeddingPort *provider)
{
    QMutexLocker locker(&m_mutex);
    m_provider = provider;
    m_seeded = false;
    m_domainVectors.clear();
    m_available = m_domains.size() == 43 && provider != nullptr;
}

void DomainClassifier::resetForTesting()
{
    QMutexLocker locker(&m_mutex);
    m_seeded = false;
    m_domainVectors.clear();
    m_available = m_domains.size() == 43 && m_provider != nullptr;
}

void DomainClassifier::invalidateCache()
{
    QMutexLocker locker(&m_mutex);
    m_seeded = false;
    m_domainVectors.clear();
}

} // namespace PersonOS::Infrastructure
