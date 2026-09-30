#include "infrastructure/knowledge/SqlKnowledgeRetrieval.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

#include "infrastructure/foundation/TimeFormat.h"
#include "infrastructure/knowledge/CjkTokenize.h"
#include "infrastructure/knowledge/SqlEmbeddingRepository.h"

namespace PersonOS::Infrastructure {

SqlKnowledgeRetrieval::SqlKnowledgeRetrieval(QSqlDatabase database, const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

void SqlKnowledgeRetrieval::setEmbeddingPort(Application::EmbeddingPort *port)
{
    m_embeddings = port;
}

Application::Result<Application::RetrievalOutput, Application::ApplicationError>
SqlKnowledgeRetrieval::retrieve(const Application::RetrievalRequest &request)
{
    const std::string now = formatUtcIso(m_clock.now());
    const std::string runUid =
        QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();

    // 1. 检索记录（running）
    QSqlQuery insertRun(m_database);
    insertRun.prepare(QStringLiteral(
        "INSERT INTO retrieval_runs_v6(uid, purpose, query_text, filters_json, "
        "strategy_version, started_at, status) VALUES(?,?,?,?,?,?,'running')"));
    insertRun.addBindValue(QString::fromStdString(runUid));
    insertRun.addBindValue(QString::fromStdString(request.purpose));
    insertRun.addBindValue(QString::fromStdString(request.queryText));
    insertRun.addBindValue(QString::fromStdString(request.filtersJson));
    insertRun.addBindValue(QString::fromStdString(request.strategyVersion));
    insertRun.addBindValue(QString::fromStdString(now));
    if (!insertRun.exec())
        return Application::Result<Application::RetrievalOutput, Application::ApplicationError>::
            failure({Application::ErrorCode::Storage, "retrieval run insert failed",
                     insertRun.lastError().text().toStdString(), false});
    const qint64 runPk = insertRun.lastInsertId().toLongLong();

    // 2. 结构化过滤（库类型/领域/状态）+ FTS5 词法召回，合并去重
    const QJsonObject filters =
        QJsonDocument::fromJson(QByteArray::fromStdString(request.filtersJson)).object();
    const QString libraryType = filters.value(QStringLiteral("library_type")).toString();
    const QString domainCode = filters.value(QStringLiteral("domain_code")).toString();

    QString sql = QStringLiteral(
        "SELECT f.owner_type, f.owner_uid, f.title, f.summary, bm25(knowledge_fts_v6) AS lex "
        "FROM knowledge_fts_v6 f "
        "JOIN knowledge_items_v5 i ON i.uid = f.owner_uid "
        "WHERE knowledge_fts_v6 MATCH ? "
        "AND i.status IN ('active','warned','candidate') ");
    if (!libraryType.isEmpty())
        sql += QStringLiteral("AND f.owner_type = ? ");
    if (!domainCode.isEmpty())
        sql += QStringLiteral("AND i.domain_code = ? ");
    sql += QStringLiteral("ORDER BY lex LIMIT 50");

    QSqlQuery search(m_database);
    search.prepare(sql);
    search.addBindValue(QString::fromStdString(Cjk::expandForQuery(request.queryText)));
    // 过滤值走绑定参数(公开端口 filtersJson 未来可能携带用户输入)
    if (!libraryType.isEmpty())
        search.addBindValue(libraryType);
    if (!domainCode.isEmpty())
        search.addBindValue(domainCode);
    if (!search.exec())
        return Application::Result<Application::RetrievalOutput, Application::ApplicationError>::
            failure({Application::ErrorCode::Storage, "fts search failed",
                     search.lastError().text().toStdString(), false});

    // FTS5 bm25() 返回带负号分数（越小越相关）：sigmoid 映射到 (0,1)，
    // 与向量余弦（[0,1]）同量纲，供跨通道融合与重排比较
    Application::RetrievalOutput output;
    std::set<std::pair<std::string, std::string>> seen;
    std::vector<Domain::RetrievalHit> lexicalHits;
    while (search.next()) {
        const std::string ownerType = search.value(0).toString().toStdString();
        const std::string ownerUid = search.value(1).toString().toStdString();
        if (!seen.insert({ownerType, ownerUid}).second)
            continue;
        Domain::RetrievalHit hit;
        hit.ownerType = ownerType;
        hit.ownerUid = ownerUid;
        const double bm25 = search.value(4).toDouble();
        hit.lexicalScore = bm25;
        hit.finalScore = 1.0 / (1.0 + std::exp(bm25));
        hit.reasonJson =
            QStringLiteral("{\"route\":\"fts_lexical\",\"bm25\":%1}")
                .arg(bm25)
                .toStdString();
        lexicalHits.push_back(std::move(hit));
    }

    // 向量语义召回（可选通道）：查询向量与同模型索引向量余弦；词法未命中的
    // owner 以向量分进入候选（DR-013：语义召回与词法互补，任一路不能单独决定）
    std::map<std::pair<std::string, std::string>, float> vectorScores;
    if (m_embeddings && m_embeddings->dimension() > 0 && !m_embeddings->modelId().empty()) {
        const auto queryVector = m_embeddings->embed(request.queryText);
        if (queryVector) {
            // 结构化条件与词法路径一致：向量行只允许命中同状态/同过滤的条目
            // （'domain' 行为领域分类专用，不参与知识检索）
            std::set<std::pair<std::string, std::string>> eligible;
            QString eligibleSql = QStringLiteral(
                "SELECT library_type, uid FROM knowledge_items_v5 "
                "WHERE status IN ('active','warned','candidate') ");
            if (!libraryType.isEmpty())
                eligibleSql += QStringLiteral("AND library_type = ? ");
            if (!domainCode.isEmpty())
                eligibleSql += QStringLiteral("AND domain_code = ? ");
            QSqlQuery eligibleQuery(m_database);
            eligibleQuery.prepare(eligibleSql);
            if (!libraryType.isEmpty())
                eligibleQuery.addBindValue(libraryType);
            if (!domainCode.isEmpty())
                eligibleQuery.addBindValue(domainCode);
            if (eligibleQuery.exec()) {
                while (eligibleQuery.next())
                    eligible.insert({eligibleQuery.value(0).toString().toStdString(),
                                     eligibleQuery.value(1).toString().toStdString()});
            }
            // 只与当前注入模型的向量空间计算余弦（不同模型空间混算无意义）
            SqlEmbeddingRepository embeddings(m_database);
            for (const auto &row : embeddings.allForModel(m_embeddings->modelId())) {
                if (row.ownerType == "domain")
                    continue;
                if (!eligible.count({row.ownerType, row.ownerUid}))
                    continue;
                const float score = cosineSimilarity(row.vector, queryVector.value());
                if (score <= 0.0f)
                    continue;
                const auto key = std::make_pair(row.ownerType, row.ownerUid);
                const auto existing = vectorScores.find(key);
                if (existing == vectorScores.end() || existing->second < score)
                    vectorScores[key] = score;
            }
        }
    }

    // 融合：词法命中并入语义分（取较大者），词法未命中的语义命中追加为
    // vector_only 候选；跨通道重排后再统一赋 rank 落库
    std::vector<Domain::RetrievalHit> hits;
    hits.reserve(lexicalHits.size() + vectorScores.size());
    for (auto &hit : lexicalHits) {
        const auto key = std::make_pair(hit.ownerType, hit.ownerUid);
        const auto vector = vectorScores.find(key);
        if (vector != vectorScores.end()) {
            hit.vectorScore = vector->second;
            // 融合：取词法与语义分较大者；路线标记入 reason
            if (*hit.vectorScore > hit.finalScore)
                hit.finalScore = *hit.vectorScore;
            hit.reasonJson =
                QStringLiteral("{\"route\":\"hybrid\",\"bm25\":%1,\"cosine\":%2}")
                    .arg(hit.lexicalScore ? *hit.lexicalScore : 0.0)
                    .arg(*hit.vectorScore)
                    .toStdString();
        }
        hits.push_back(std::move(hit));
    }
    for (const auto &entry : vectorScores) {
        if (seen.count(entry.first))
            continue;
        Domain::RetrievalHit hit;
        hit.ownerType = entry.first.first;
        hit.ownerUid = entry.first.second;
        hit.vectorScore = entry.second;
        hit.finalScore = entry.second;
        hit.reasonJson = QStringLiteral("{\"route\":\"vector_only\",\"cosine\":%1}")
                             .arg(entry.second)
                             .toStdString();
        hits.push_back(std::move(hit));
    }

    // 跨通道重排：finalScore 降序（稳定排序保持词法通道内 bm25 序；纯词法
    // 查询时与原有顺序一致，无回归）
    std::stable_sort(hits.begin(), hits.end(),
                     [](const Domain::RetrievalHit &a, const Domain::RetrievalHit &b) {
                         return a.finalScore > b.finalScore;
                     });

    int rank = 1;
    for (auto &hit : hits) {
        hit.rank = rank;
        QSqlQuery insertHit(m_database);
        insertHit.prepare(QStringLiteral(
            "INSERT INTO retrieval_hits_v6(run_id, owner_type, owner_uid, lexical_score, "
            "vector_score, final_score, rank, reason_json) VALUES(?,?,?,?,?,?,?,?)"));
        insertHit.addBindValue(runPk);
        insertHit.addBindValue(QString::fromStdString(hit.ownerType));
        insertHit.addBindValue(QString::fromStdString(hit.ownerUid));
        insertHit.addBindValue(hit.lexicalScore ? *hit.lexicalScore : 0.0);
        insertHit.addBindValue(hit.vectorScore ? QVariant(*hit.vectorScore) : QVariant());
        insertHit.addBindValue(hit.finalScore);
        insertHit.addBindValue(rank);
        insertHit.addBindValue(QString::fromStdString(hit.reasonJson));
        if (!insertHit.exec())
            return Application::Result<Application::RetrievalOutput,
                                       Application::ApplicationError>::
                failure({Application::ErrorCode::Storage, "retrieval hit insert failed",
                         insertHit.lastError().text().toStdString(), false});
        output.hits.push_back(std::move(hit));
        ++rank;
    }

    // 3. 完成检索记录
    QSqlQuery complete(m_database);
    complete.prepare(QStringLiteral(
        "UPDATE retrieval_runs_v6 SET status='completed', completed_at=? WHERE id=?"));
    complete.addBindValue(QString::fromStdString(formatUtcIso(m_clock.now())));
    complete.addBindValue(runPk);
    if (!complete.exec())
        return Application::Result<Application::RetrievalOutput, Application::ApplicationError>::
            failure({Application::ErrorCode::Storage, "retrieval run complete failed",
                     complete.lastError().text().toStdString(), false});

    Domain::RetrievalRun run;
    run.uid = *Domain::Uid::parse(runUid);
    run.purpose = request.purpose;
    run.queryText = request.queryText;
    run.filtersJson = request.filtersJson;
    run.strategyVersion = request.strategyVersion;
    run.status = Domain::RetrievalStatus::Completed;
    run.startedAt = now;
    run.completedAt = formatUtcIso(m_clock.now());
    output.run = std::move(run);
    return Application::Result<Application::RetrievalOutput, Application::ApplicationError>::
        success(std::move(output));
}

Application::Result<Domain::KnowledgeSnapshot, Application::ApplicationError>
SqlKnowledgeRetrieval::snapshot(const std::string &purpose,
                                const std::string &manifestVersionsJson,
                                const std::string &knowledgeVersionsJson,
                                const std::optional<std::string> &retrievalRunUid)
{
    const QByteArray content = QByteArray::fromStdString(
        purpose + "\n" + manifestVersionsJson + "\n" + knowledgeVersionsJson);
    const std::string contentHash =
        QCryptographicHash::hash(content, QCryptographicHash::Sha256)
            .toHex()
            .toStdString();

    // 同内容幂等：返回已有快照
    QSqlQuery existing(m_database);
    existing.prepare(QStringLiteral(
        "SELECT uid, created_at, purpose, manifest_versions_json, knowledge_versions_json, "
        "state_snapshot_uid, retrieval_run_uid, content_hash "
        "FROM knowledge_snapshots_v6 WHERE content_hash=?"));
    existing.addBindValue(QString::fromStdString(contentHash));
    if (existing.exec() && existing.next()) {
        Domain::KnowledgeSnapshot snapshot;
        if (const auto uid = Domain::Uid::parse(existing.value(0).toString().toStdString()))
            snapshot.uid = *uid;
        snapshot.createdAt = existing.value(1).toString().toStdString();
        snapshot.purpose = existing.value(2).toString().toStdString();
        snapshot.manifestVersionsJson = existing.value(3).toString().toStdString();
        snapshot.knowledgeVersionsJson = existing.value(4).toString().toStdString();
        if (!existing.value(5).isNull())
            snapshot.stateSnapshotUid = existing.value(5).toString().toStdString();
        if (!existing.value(6).isNull())
            snapshot.retrievalRunUid = existing.value(6).toString().toStdString();
        snapshot.contentHash = contentHash;
        return Application::Result<Domain::KnowledgeSnapshot, Application::ApplicationError>::
            success(std::move(snapshot));
    }

    const std::string now = formatUtcIso(m_clock.now());
    const std::string snapshotUid =
        QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO knowledge_snapshots_v6(uid, created_at, purpose, manifest_versions_json, "
        "knowledge_versions_json, state_snapshot_uid, retrieval_run_uid, content_hash) "
        "VALUES(?,?,?,?,?,?,?,?)"));
    query.addBindValue(QString::fromStdString(snapshotUid));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(purpose));
    query.addBindValue(QString::fromStdString(manifestVersionsJson));
    query.addBindValue(QString::fromStdString(knowledgeVersionsJson));
    query.addBindValue(QVariant());
    query.addBindValue(retrievalRunUid
                           ? QVariant(QString::fromStdString(*retrievalRunUid))
                           : QVariant());
    query.addBindValue(QString::fromStdString(contentHash));
    if (!query.exec())
        return Application::Result<Domain::KnowledgeSnapshot, Application::ApplicationError>::
            failure({Application::ErrorCode::Storage, "knowledge snapshot insert failed",
                     query.lastError().text().toStdString(), false});

    Domain::KnowledgeSnapshot snapshot;
    snapshot.uid = *Domain::Uid::parse(snapshotUid);
    snapshot.createdAt = now;
    snapshot.purpose = purpose;
    snapshot.manifestVersionsJson = manifestVersionsJson;
    snapshot.knowledgeVersionsJson = knowledgeVersionsJson;
    snapshot.retrievalRunUid = retrievalRunUid;
    snapshot.contentHash = contentHash;
    return Application::Result<Domain::KnowledgeSnapshot, Application::ApplicationError>::
        success(std::move(snapshot));
}

} // namespace PersonOS::Infrastructure
