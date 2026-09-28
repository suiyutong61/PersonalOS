#include "presentation/viewmodels/MelViewModel.h"

#include <QVariantMap>

#include <QtConcurrent/QtConcurrent>

#include "application/usecases/mel/MelUseCases.h"
#include "application/usecases/planning/AiPlanningUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/persistence/DatabaseConnectionFactory.h"
#include "presentation/viewmodels/AiServices.h"
#include "domain/foundation/Uid.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/persistence/SqlMelRepository.h"
#include "presentation/viewmodels/VmSupport.h"

namespace PersonOS {

namespace {

QVariantMap taskRow(const QString &uid, const QString &title, double progress, bool required,
                    const QString &state)
{
    return {{QStringLiteral("uid"), uid},
            {QStringLiteral("title"), title},
            {QStringLiteral("subtitle"), state},
            {QStringLiteral("badge"), PersonOS::Presentation::percentText(progress)},
            {QStringLiteral("badgeTone"), progress >= 1.0 ? QStringLiteral("success")
                                                          : QStringLiteral("info")},
            {QStringLiteral("detail"),
             required ? QStringLiteral("必做") : QStringLiteral("可选")}};
}

} // namespace

MelViewModel::MelViewModel(QObject *parent) : QObject(parent) {}

void MelViewModel::setState(const QString &state)
{
    if (m_pageState == state)
        return;
    m_pageState = state;
    emit pageStateChanged();
}

void MelViewModel::setError(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
}

void MelViewModel::refresh()
{
    setState(QStringLiteral("loading"));
    setError({});

    const auto database = DatabaseManager::instance().database();
    const auto userUid = Presentation::activeUserUid(database);
    if (!userUid) {
        m_tasksModel.clear();
        m_melUid.clear();
        setState(QStringLiteral("empty"));
        return;
    }

    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlMelRepository melRepo(database, clock);
    const auto activeMels = melRepo.findActive(*userUid, 1);
    if (activeMels.empty()) {
        m_tasksModel.clear();
        m_melUid.clear();
        m_melTitle.clear();
        m_melState.clear();
        m_melDeadline.clear();
        m_melProgress.clear();
        setState(QStringLiteral("empty"));
        emit melChanged();
        return;
    }

    const Domain::Mel &mel = activeMels.front();
    m_melUid = QString::fromStdString(mel.uid.value());
    m_melTitle = QString::fromStdString(mel.title);
    m_melState = QString::fromStdString(Domain::toString(mel.state));
    m_melDeadline = QString::fromStdString(mel.plannedEndAt);
    m_melRevision = mel.revision;

    const auto tasks = melRepo.tasksOf(mel.uid);
    double progressSum = 0.0;
    int requiredCount = 0;
    QVariantList rows;
    for (const auto &task : tasks) {
        if (task.required) {
            progressSum += task.progress;
            ++requiredCount;
        }
        rows.append(taskRow(QString::fromStdString(task.uid.value()),
                            QString::fromStdString(task.title), task.progress,
                            task.required, QString::fromStdString(Domain::toString(task.state))));
    }
    m_tasksModel.replace(rows);
    m_melProgress = requiredCount > 0
                        ? Presentation::percentText(progressSum / requiredCount)
                        : Presentation::percentText(0.0);
    setState(QStringLiteral("ready"));
    emit melChanged();
    emit dataChanged();
}

void MelViewModel::recordProgress(const QString &taskUid, int percent, const QString &note)
{
    const auto parsedTask = Domain::Uid::parse(taskUid.toStdString());
    const auto parsedMel = Domain::Uid::parse(m_melUid.toStdString());
    if (!parsedTask || !parsedMel || percent < 0 || percent > 100) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("进度必须在 0–100 之间"));
        return;
    }

    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlMelRepository melRepo(database, clock);
    Application::MelUseCases useCases(melRepo, uids, clock);

    Application::MelUseCases::ProgressInput input;
    input.taskUid = *parsedTask;
    input.progress = percent / 100.0;
    input.note = note.trimmed().toStdString();
    input.idempotencyKey = "ui:" + uids.next().value();
    const auto updated = useCases.recordProgress(*parsedMel, input);
    if (!updated) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(updated.error().message, updated.error().detail));
        return;
    }
    refresh();
}

void MelViewModel::completeExecution()
{
    const auto parsed = Domain::Uid::parse(m_melUid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("当前没有可操作的 MEL"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlMelRepository melRepo(database, clock);
    Application::MelUseCases useCases(melRepo, uids, clock);
    const auto completed = useCases.completeExecution(*parsed, m_melRevision);
    if (!completed) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(completed.error().message, completed.error().detail));
        return;
    }
    refresh();
}

void MelViewModel::settle()
{
    const auto parsed = Domain::Uid::parse(m_melUid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("当前没有可结算的 MEL"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlMelRepository melRepo(database, clock);
    Application::MelUseCases useCases(melRepo, uids, clock);
    const auto settled = useCases.settleMel(*parsed, m_melRevision);
    if (!settled) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(settled.error().message, settled.error().detail));
        return;
    }
    refresh();
}

void MelViewModel::pauseResume()
{
    const auto parsed = Domain::Uid::parse(m_melUid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("当前没有可暂停/恢复的 MEL"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlMelRepository melRepo(database, clock);
    Application::MelUseCases useCases(melRepo, uids, clock);

    const auto current = melRepo.findByUid(*parsed);
    if (!current) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("MEL 不存在"));
        return;
    }
    const auto updated = current->state == Domain::MelState::Paused
                             ? useCases.resumeMel(*parsed, m_melRevision)
                             : useCases.pauseMel(*parsed, m_melRevision);
    if (!updated) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(updated.error().message, updated.error().detail));
        return;
    }
    refresh();
}


void MelViewModel::runAi(const QString &purpose, const QString &jobType)
{
    // jobType: "mel" | "methods"；每线程独立数据库连接
    const auto melUid = Domain::Uid::parse(m_melUid.toStdString());
    const auto userUid = Presentation::activeUserUid(DatabaseManager::instance().database());
    if (!userUid) {
        setState(QStringLiteral("empty"));
        setError(QStringLiteral("缺少用户档案"));
        return;
    }
    m_aiState = QStringLiteral("ai_waiting");
    emit aiStateChanged();

    const QString dbPath = DatabaseManager::instance().databasePath();
    QtConcurrent::run([this, purpose, jobType, userUid = *userUid,
                       melUid, dbPath]() {
        QString error;
        Infrastructure::DatabaseConnectionFactory factory(dbPath);
        QString openError;
        QSqlDatabase workerDb = factory.openForCurrentThread(purpose, &openError);
        if (!workerDb.isOpen())
            return QStringLiteral("数据库连接失败：") + openError;
        const auto pipeline = Presentation::buildPlanningPipeline(workerDb);
        if (jobType == QStringLiteral("mel")) {
            // 取首个目标生成候选 MEL
            const auto goals = pipeline->goalsRepo->findByUser(userUid);
            if (goals.empty()) {
                Infrastructure::DatabaseConnectionFactory::closeCurrentThreadConnection(
                    purpose);
                return QStringLiteral("请先创建学习目标");
            }
            const auto result =
                pipeline->useCases->generateMelProposal(userUid, goals.front().uid,
                                                        std::nullopt);
            if (!result)
                error = Presentation::friendlyError(result.error().message,
                                                    result.error().detail);
        } else if (melUid) {
            const auto bound = pipeline->useCases->generateMethodSuggestions(userUid, *melUid);
            if (!bound)
                error = Presentation::friendlyError(bound.error().message,
                                                    bound.error().detail);
        } else {
            error = QStringLiteral("当前没有可关联方法的 MEL");
        }
        Infrastructure::DatabaseConnectionFactory::closeCurrentThreadConnection(purpose);
        return error;
    }).then([this](QString error) {
        if (error.isEmpty()) {
            refresh();
        } else {
            setState(QStringLiteral("error"));
            setError(error);
        }
        m_aiState = QStringLiteral("idle");
        emit aiStateChanged();
    });
}

void MelViewModel::aiGenerateMel()
{
    runAi(QStringLiteral("ai_mel"), QStringLiteral("mel"));
}

void MelViewModel::aiSuggestMethods()
{
    runAi(QStringLiteral("ai_methods"), QStringLiteral("methods"));
}

} // namespace PersonOS
