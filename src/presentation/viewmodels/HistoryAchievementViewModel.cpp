#include "presentation/viewmodels/HistoryAchievementViewModel.h"

#include <QVariantMap>

#include "database/DatabaseManager.h"
#include "domain/foundation/Uid.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/operations/SqlOperationsRepository.h"
#include "infrastructure/persistence/SqlReviewRepository.h"
#include "presentation/viewmodels/VmSupport.h"

namespace PersonOS {

namespace {

QVariantMap row(const QString &uid, const QString &title, const QString &subtitle,
                const QString &badge, const QString &badgeTone, const QString &detail = {})
{
    return {{QStringLiteral("uid"), uid},
            {QStringLiteral("title"), title},
            {QStringLiteral("subtitle"), subtitle},
            {QStringLiteral("detail"), detail},
            {QStringLiteral("badge"), badge},
            {QStringLiteral("badgeTone"), badgeTone}};
}

} // namespace

HistoryAchievementViewModel::HistoryAchievementViewModel(QObject *parent) : QObject(parent) {}

void HistoryAchievementViewModel::setState(const QString &state)
{
    if (m_pageState == state)
        return;
    m_pageState = state;
    emit pageStateChanged();
}

void HistoryAchievementViewModel::refresh()
{
    setState(QStringLiteral("loading"));
    m_lastError.clear();
    emit lastErrorChanged();

    const auto database = DatabaseManager::instance().database();
    const auto userUid = Presentation::activeUserUid(database);
    Infrastructure::QtSystemClock clock;
    if (!userUid) {
        m_achievementsModel.clear();
        m_timelineModel.clear();
        setState(QStringLiteral("empty"));
        return;
    }

    Infrastructure::SqlOperationsRepository opsRepo(database, clock);
    Infrastructure::SqlReviewRepository reviewRepo(database, clock);

    // 成就：只来自真实业务事件
    QVariantList achievementRows;
    for (const auto &achievement : opsRepo.listForUser(*userUid, 50)) {
        achievementRows.append(row(
            QString::fromStdString(achievement.uid.value()),
            QString::fromStdString(achievement.title),
            QString::fromStdString(achievement.description),
            Presentation::displayDateTime(QString::fromStdString(achievement.earnedAt)),
            QStringLiteral("success"),
            Presentation::achievementTypeLabel(
                QString::fromStdString(achievement.achievementType))));
    }

    // 复盘时间线（最近的复盘，含下一动作）
    QVariantList timelineRows;
    for (const auto &review : reviewRepo.listRecent(50)) {
        const QString status = QString::fromStdString(Domain::toString(review.status));
        timelineRows.append(row(
            QString::fromStdString(review.uid.value()),
            QStringLiteral("复盘 %1").arg(Presentation::displayDateTime(
                QString::fromStdString(review.startedAt))),
            QString::fromStdString(review.summary),
            Presentation::reviewStatusLabel(status),
            status == QStringLiteral("closed") ? QStringLiteral("success")
                                               : QStringLiteral("neutral"),
            QString::fromStdString(review.nextAction)));
    }

    m_achievementsModel.replace(achievementRows);
    m_timelineModel.replace(timelineRows);
    setState((achievementRows.isEmpty() && timelineRows.isEmpty()) ? QStringLiteral("empty")
                                                                   : QStringLiteral("ready"));
    emit dataChanged();
}

} // namespace PersonOS
