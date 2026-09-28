#include "presentation/viewmodels/CalendarViewModel.h"

#include <QVariantMap>

#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/operations/SqlOperationsRepository.h"
#include "infrastructure/persistence/SqlMelRepository.h"
#include "presentation/viewmodels/VmSupport.h"

namespace PersonOS {

namespace {

QVariantMap row(const QString &uid, const QString &title, const QString &subtitle,
                const QString &badge, const QString &badgeTone)
{
    return {{QStringLiteral("uid"), uid},
            {QStringLiteral("title"), title},
            {QStringLiteral("subtitle"), subtitle},
            {QStringLiteral("badge"), badge},
            {QStringLiteral("badgeTone"), badgeTone}};
}

} // namespace

CalendarViewModel::CalendarViewModel(QObject *parent) : QObject(parent) {}

void CalendarViewModel::setState(const QString &state)
{
    if (m_pageState == state)
        return;
    m_pageState = state;
    emit pageStateChanged();
}

void CalendarViewModel::refresh()
{
    setState(QStringLiteral("loading"));
    m_lastError.clear();
    emit lastErrorChanged();

    const auto database = DatabaseManager::instance().database();
    const auto userUid = Presentation::activeUserUid(database);
    Infrastructure::QtSystemClock clock;
    if (!userUid) {
        m_remindersModel.clear();
        m_dueModel.clear();
        setState(QStringLiteral("empty"));
        return;
    }

    Infrastructure::SqlOperationsRepository opsRepo(database, clock);
    Infrastructure::SqlMelRepository melRepo(database, clock);

    // 已启用提醒规则
    QVariantList reminderRows;
    for (const auto &rule : opsRepo.enabledRules()) {
        reminderRows.append(row(
            QString::fromStdString(rule.uid.value()),
            QStringLiteral("提醒：%1 前 %2 分钟").arg(
                QString::fromStdString(rule.ownerType), QString::number(rule.offsetMin)),
            QString::fromStdString(rule.ownerUid), QString::fromStdString(rule.channel),
            QStringLiteral("info")));
    }

    // 待投递 + MEL Deadline 事件
    QVariantList dueRows;
    for (const auto &delivery : opsRepo.pendingDeliveries(clock.utcIso())) {
        dueRows.append(row(QString::fromStdString(delivery.uid.value()),
                           QStringLiteral("待投递提醒"),
                           QString::fromStdString(delivery.scheduledAt),
                           QStringLiteral("待投递"), QStringLiteral("warning")));
    }
    for (const auto &mel : melRepo.findDue(clock.utcIso(), 20)) {
        dueRows.append(row(QString::fromStdString(mel.uid.value()),
                           QStringLiteral("MEL Deadline：%1").arg(
                               QString::fromStdString(mel.title)),
                           QString::fromStdString(mel.plannedEndAt),
                           QStringLiteral("已到期"), QStringLiteral("error")));
    }

    m_remindersModel.replace(reminderRows);
    m_dueModel.replace(dueRows);
    setState((reminderRows.isEmpty() && dueRows.isEmpty()) ? QStringLiteral("empty")
                                                           : QStringLiteral("ready"));
    emit dataChanged();
}

} // namespace PersonOS
