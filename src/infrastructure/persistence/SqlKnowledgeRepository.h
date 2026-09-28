#pragma once

#include <QSqlDatabase>

#include "application/ports/KnowledgeRepository.h"
#include "domain/foundation/Clock.h"

namespace PersonOS::Infrastructure {

class SqlKnowledgeRepository final : public Application::KnowledgeRepository
{
public:
    explicit SqlKnowledgeRepository(QSqlDatabase database, const Domain::Clock &clock);

    Application::SaveResult beginWrite() override;
    Application::SaveResult commitWrite() override;
    void rollbackWrite() override;

    std::optional<Domain::KnowledgeItem> findItem(const Domain::Uid &uid) override;
    std::vector<Domain::KnowledgeItem> findItemsByTitle(const std::string &title) override;
    std::vector<Domain::KnowledgeItem> listRecent(int limit) override;
    Application::SaveResult insertItem(const Domain::KnowledgeItem &item) override;
    Application::SaveResult updateItem(const Domain::KnowledgeItem &item,
                                       int expectedRevision) override;

    Application::Result<std::string, Application::ApplicationError> insertVersion(
        const Domain::KnowledgeVersion &version) override;
    std::vector<Domain::KnowledgeVersion> versionsOf(const Domain::Uid &itemUid) override;

    Application::SaveResult insertSource(const Domain::SourceRecord &source) override;
    Application::SaveResult insertFragment(const Domain::EvidenceFragment &fragment) override;
    Application::SaveResult insertEvidenceLink(const Domain::EvidenceLink &link) override;

    Application::SaveResult insertRelation(const Domain::KnowledgeRelation &relation) override;
    bool relationExists(const Domain::KnowledgeRelation &relation) override;
    std::vector<Domain::KnowledgeRelation> relationsOf(const Domain::Uid &itemUid) override;

    Application::SaveResult insertPaperDetail(const Domain::PaperDetail &detail) override;
    Application::SaveResult insertMethodDetail(const Domain::MethodDetail &detail) override;
    Application::SaveResult insertTipDetail(const Domain::TipDetail &detail) override;
    Application::SaveResult insertPlanDetail(const Domain::PlanDetail &detail) override;
    Application::SaveResult insertMethodStep(const Domain::MethodStep &step) override;

private:
    Application::SaveResult writeFailure(const char *operation,
                                         const class QSqlQuery &query) const;
    std::optional<qint64> resolvePk(const char *sql, const std::string &uid) const;

    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
    int m_transactionDepth = 0;
};

} // namespace PersonOS::Infrastructure
