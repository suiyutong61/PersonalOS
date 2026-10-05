#include "presentation/viewmodels/CalendarViewModel.h"

#include <QVariantMap>

#include "application/usecases/operations/OperationUseCases.h"
#include "database/DatabaseManager.h"
#include "domain/foundation/Uid.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/operations/SqlOperationsRepository.h"
#include "infrastructure/persistence/SqlMelRepository.h"
#include "presentation/viewmodels/AppNotifier.h"
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

QString deliveryTone(const std::string &status)
{
    if (status == "delivered")
        return QStringLiteral("success");
    if (status == "failed")
        return QStringLiteral("error");
    if (status == "suppressed")
        return QStringLiteral("neutral");
    return QStringLiteral("warning");   // pending
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
        m_deliveriesModel.clear();
        m_activeMelsModel.clear();
        setState(QStringLiteral("empty"));
        return;
    }

    Infrastructure::SqlOperationsRepository opsRepo(database, clock);
    Infrastructure::SqlMelRepository melRepo(database, clock);

    // 全部提醒规则（含已停用；v1 只绑定 MEL，展示 MEL 标题而非内部
    // UUID/英文类型。kind 保留原始键供 QML 判断停用/启用按钮）
    QVariantList reminderRows;
    for (const auto &rule : opsRepo.allRules()) {
        QString title = QStringLiteral("提醒：前 %1 分钟").arg(rule.offsetMin);
        QString subtitle = QStringLiteral("应用内");
        if (rule.ownerType == "mel") {
            if (const auto melUid = Domain::Uid::parse(rule.ownerUid))
                if (const auto mel = melRepo.findByUid(*melUid)) {
                    title = QStringLiteral("Deadline 提醒：%1").arg(
                        QString::fromStdString(mel->title));
                    subtitle = rule.offsetMin > 0
                                   ? QStringLiteral("提前 %1 分钟 · 应用内")
                                         .arg(rule.offsetMin)
                                   : QStringLiteral("到达时 · 应用内");
                }
        }
        QVariantMap ruleRow =
            row(QString::fromStdString(rule.uid.value()), title, subtitle,
                rule.enabled ? QStringLiteral("已启用") : QStringLiteral("已停用"),
                rule.enabled ? QStringLiteral("info") : QStringLiteral("neutral"));
        ruleRow.insert(QStringLiteral("kind"),
                       rule.enabled ? QStringLiteral("enabled")
                                    : QStringLiteral("disabled"));
        reminderRows.append(ruleRow);
    }

    // 活跃 MEL（新建提醒的选择来源）
    QVariantList activeMelRows;
    for (const auto &mel : melRepo.findActive(*userUid, 50)) {
        activeMelRows.append(row(
            QString::fromStdString(mel.uid.value()), QString::fromStdString(mel.title),
            QStringLiteral("Deadline：%1").arg(Presentation::displayDateTime(
                QString::fromStdString(mel.plannedEndAt))),
            QStringLiteral("进行中"), QStringLiteral("info")));
    }

    // 待投递 + MEL Deadline 事件(时间为本地可读格式)
    QVariantList dueRows;
    for (const auto &delivery : opsRepo.pendingDeliveries(clock.utcIso())) {
        dueRows.append(row(QString::fromStdString(delivery.uid.value()),
                           QStringLiteral("待投递提醒"),
                           Presentation::displayDateTime(
                               QString::fromStdString(delivery.scheduledAt)),
                           QStringLiteral("待投递"), QStringLiteral("warning")));
    }
    for (const auto &mel : melRepo.findDue(clock.utcIso(), 20)) {
        dueRows.append(row(QString::fromStdString(mel.uid.value()),
                           QStringLiteral("MEL Deadline：%1").arg(
                               QString::fromStdString(mel.title)),
                           Presentation::displayDateTime(
                               QString::fromStdString(mel.plannedEndAt)),
                           QStringLiteral("已到期"), QStringLiteral("error")));
    }

    // 投递历史（最近 20 条，最新在前；状态如实展示，失败附原因）
    QVariantList deliveryRows;
    for (const auto &delivery : opsRepo.recentDeliveries(20)) {
        const QString status = QString::fromStdString(delivery.status);
        deliveryRows.append(row(
            QString::fromStdString(delivery.uid.value()),
            QStringLiteral("提醒：%1").arg(Presentation::displayDateTime(
                QString::fromStdString(delivery.scheduledAt))),
            delivery.error ? QString::fromStdString(*delivery.error) : QString(),
            Presentation::reminderDeliveryStatusLabel(status),
            deliveryTone(delivery.status)));
    }

    m_remindersModel.replace(reminderRows);
    m_dueModel.replace(dueRows);
    m_deliveriesModel.replace(deliveryRows);
    m_activeMelsModel.replace(activeMelRows);
    setState((reminderRows.isEmpty() && dueRows.isEmpty() && deliveryRows.isEmpty())
                 ? QStringLiteral("empty")
                 : QStringLiteral("ready"));
    emit dataChanged();
}

void CalendarViewModel::createMelReminder(const QString &melUid, int offsetMin)
{
    const auto parsed = Domain::Uid::parse(melUid.toStdString());
    if (!parsed || offsetMin < 0) {
        setState(QStringLiteral("conflict"));
        m_lastError = QStringLiteral("请选择 MEL 并填写不小于 0 的提前量");
        emit lastErrorChanged();
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlMelRepository melRepo(database, clock);
    Infrastructure::SqlOperationsRepository opsRepo(database, clock);
    Application::ReminderService reminders(opsRepo, melRepo,
                                           Presentation::AppNotifier::instance(),
                                           uids, clock);
    Application::ReminderService::RuleInput input;
    input.ownerType = "mel";
    input.ownerUid = melUid.toStdString();
    input.offsetMin = offsetMin;
    input.channel = "app";
    const auto created = reminders.createRule(input);
    if (!created) {
        setState(QStringLiteral("error"));
        m_lastError = Presentation::friendlyError(created.error().message,
                                                  created.error().detail);
        emit lastErrorChanged();
        return;
    }
    refresh();
}

void CalendarViewModel::setRuleEnabled(const QString &ruleUid, bool enabled)
{
    const auto parsed = Domain::Uid::parse(ruleUid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("error"));
        m_lastError = QStringLiteral("无效的提醒标识");
        emit lastErrorChanged();
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlMelRepository melRepo(database, clock);
    Infrastructure::SqlOperationsRepository opsRepo(database, clock);
    Application::ReminderService reminders(opsRepo, melRepo,
                                           Presentation::AppNotifier::instance(),
                                           uids, clock);
    const auto current = opsRepo.findRule(*parsed);
    if (!current) {
        setState(QStringLiteral("error"));
        m_lastError = QStringLiteral("提醒不存在");
        emit lastErrorChanged();
        return;
    }
    const auto result = enabled ? reminders.enableRule(*parsed, current->revision)
                                : reminders.disableRule(*parsed, current->revision);
    if (!result) {
        setState(QStringLiteral("error"));
        m_lastError = Presentation::friendlyError(result.error().message,
                                                  result.error().detail);
        emit lastErrorChanged();
        return;
    }
    refresh();
}

} // namespace PersonOS
