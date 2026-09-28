#include "presentation/viewmodels/KnowledgeViewModel.h"

#include <QVariantMap>

#include "application/usecases/knowledge/KnowledgeUseCases.h"
#include "database/DatabaseManager.h"
#include "domain/foundation/Uid.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/SqlKnowledgeFtsIndex.h"
#include "infrastructure/persistence/SqlKnowledgeRepository.h"
#include "presentation/viewmodels/VmSupport.h"

namespace PersonOS {

namespace {

QVariantMap row(const Domain::KnowledgeItem &item)
{
    const QString type = QString::fromStdString(Domain::toString(item.libraryType));
    return {{QStringLiteral("uid"), QString::fromStdString(item.uid.value())},
            {QStringLiteral("title"), QString::fromStdString(item.title)},
            {QStringLiteral("subtitle"), QString::fromStdString(item.domainCode)},
            {QStringLiteral("badge"), type},
            {QStringLiteral("badgeTone"),
             item.status == Domain::KnowledgeStatus::Warned ? QStringLiteral("warning")
                                                            : QStringLiteral("info")},
            {QStringLiteral("detail"), QString::fromStdString(Domain::toString(item.status))}};
}

} // namespace

KnowledgeViewModel::KnowledgeViewModel(QObject *parent) : QObject(parent) {}

void KnowledgeViewModel::setState(const QString &state)
{
    if (m_pageState == state)
        return;
    m_pageState = state;
    emit pageStateChanged();
}

void KnowledgeViewModel::setError(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
}

void KnowledgeViewModel::showItems(const std::vector<Domain::KnowledgeItem> &items)
{
    QVariantList rows;
    for (const auto &item : items)
        rows.append(row(item));
    m_itemsModel.replace(rows);
    setState(rows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"));
    emit dataChanged();
}

void KnowledgeViewModel::refresh()
{
    setState(QStringLiteral("loading"));
    setError({});
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlKnowledgeRepository repo(database, clock);
    showItems(repo.listRecent(100));
}

void KnowledgeViewModel::search(const QString &text)
{
    setState(QStringLiteral("loading"));
    setError({});
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlKnowledgeRepository repo(database, clock);
    if (text.trimmed().isEmpty()) {
        showItems(repo.listRecent(100));
        return;
    }
    showItems(repo.findItemsByTitle(text.trimmed().toStdString()));
}

void KnowledgeViewModel::addItem(const QString &kind, const QString &title,
                                 const QString &summary)
{
    const auto type = Domain::libraryTypeFrom(kind.toStdString());
    if (!type || title.trimmed().isEmpty() || summary.trimmed().isEmpty()) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("请选择类型并填写标题与摘要"));
        return;
    }
    if (*type == Domain::LibraryType::Paper) {
        // 论文走单文献处理核心（PDF/文本摄入），不在本表单直接建条目
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("论文请通过导入文件添加（经单文献处理核心）"));
        return;
    }

    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlKnowledgeRepository repo(database, clock);
    Infrastructure::SqlKnowledgeFtsIndex index(database, clock);
    Application::KnowledgeUseCases useCases(repo, index, uids, clock);

    Application::KnowledgeUseCases::ImportInput input;
    input.libraryType = *type;
    input.title = title.trimmed().toStdString();
    input.domainCode = "learning";
    input.summary = summary.trimmed().toStdString();
    input.contentHash = std::string("manual:") + uids.next().value();
    input.createdBy = "user";
    const auto imported = useCases.importKnowledge(input);
    if (!imported) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(imported.error().message,
                                             imported.error().detail));
        return;
    }
    refresh();
}

} // namespace PersonOS
