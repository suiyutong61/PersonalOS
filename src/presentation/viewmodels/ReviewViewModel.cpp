#include "presentation/viewmodels/ReviewViewModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include "application/usecases/assessment/AssessmentUseCases.h"
#include "application/usecases/review/QuestionnaireUseCases.h"
#include "application/usecases/review/ReviewUseCases.h"
#include "database/DatabaseManager.h"
#include "domain/foundation/Uid.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/ReviewQuestionnaireSeed.h"
#include "infrastructure/persistence/SqlAssessmentRepository.h"
#include "infrastructure/persistence/SqlMelRepository.h"
#include "infrastructure/persistence/SqlQuestionnaireRepository.h"
#include "infrastructure/persistence/SqlReviewRepository.h"
#include "presentation/viewmodels/VmSupport.h"

namespace PersonOS {

namespace {

const char *kChoiceTone(const QString &choice)
{
    if (choice == QStringLiteral("answer"))
        return "info";
    if (choice == QStringLiteral("no_change"))
        return "success";
    if (choice == QStringLiteral("skipped"))
        return "neutral";
    return "neutral";
}

} // namespace

ReviewViewModel::ReviewViewModel(QObject *parent) : QObject(parent) {}

void ReviewViewModel::setState(const QString &state)
{
    if (m_pageState == state)
        return;
    m_pageState = state;
    emit pageStateChanged();
}

void ReviewViewModel::setError(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
}

void ReviewViewModel::setReviewSummary(const QString &value)
{
    if (m_reviewSummary == value)
        return;
    m_reviewSummary = value;
    emit reviewChanged();
}

void ReviewViewModel::setReviewProblems(const QString &value)
{
    if (m_reviewProblems == value)
        return;
    m_reviewProblems = value;
    emit reviewChanged();
}

void ReviewViewModel::setReviewNextActions(const QString &value)
{
    if (m_reviewNextActions == value)
        return;
    m_reviewNextActions = value;
    emit reviewChanged();
}

void ReviewViewModel::refresh()
{
    setState(QStringLiteral("loading"));
    setError({});

    const auto database = DatabaseManager::instance().database();
    const auto userUid = Presentation::activeUserUid(database);
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    if (!userUid) {
        m_questionnaireModel.clear();
        m_assessmentsModel.clear();
        m_reviewUid.clear();
        setState(QStringLiteral("empty"));
        return;
    }

    // 复盘问卷种子（基本+十类 16 项；幂等）
    Infrastructure::ReviewQuestionnaireSeed questionnaireSeed(database, clock);
    if (!questionnaireSeed.ensureSeeded()) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("复盘问卷种子初始化失败"));
        return;
    }

    Infrastructure::SqlReviewRepository reviewRepo(database, clock);
    Infrastructure::SqlQuestionnaireRepository questionnaireRepo(database, clock);
    Infrastructure::SqlAssessmentRepository assessmentRepo(database, clock);
    Infrastructure::SqlMelRepository melRepo(database, clock);
    Application::ReviewUseCases reviewUseCases(reviewRepo, uids, clock);
    Application::QuestionnaireUseCases questionnaireUseCases(questionnaireRepo, reviewRepo,
                                                             uids, clock);
    Application::AssessmentUseCases assessmentUseCases(assessmentRepo, uids, clock);

    // 最近的复盘（历史最近一个，含已关闭）
    const auto recent = reviewRepo.listRecent(1);
    if (recent.empty()) {
        m_reviewUid.clear();
        m_reviewState.clear();
        m_questionnaireModel.clear();
        m_assessmentsModel.clear();
        setState(QStringLiteral("empty"));
        return;
    }

    const Domain::Review &review = recent.front();
    m_reviewUid = QString::fromStdString(review.uid.value());
    m_reviewState = QString::fromStdString(Domain::toString(review.status));
    m_reviewSummary = QString::fromStdString(review.summary);
    m_reviewProblems.clear();
    m_reviewNextActions = QString::fromStdString(review.nextAction);

    // 问卷与已提交应答
    const auto questionnaire =
        questionnaireUseCases.findByCodeVersion("review_extended_states", 1);
    if (questionnaire) {
        m_questionnaireUid = QString::fromStdString(questionnaire.value().uid.value());
        m_questionnaireCodes.clear();
        m_questionnaireChoices.clear();
        for (const auto &code : questionnaireRepo.itemCodesOf(questionnaire.value().uid))
            m_questionnaireCodes.append(QString::fromStdString(code));
        const auto response = questionnaireRepo.findResponse(review.uid,
                                                             questionnaire.value().uid);
        if (response) {
            const auto sections =
                questionnaireRepo.parseSections(response->responsesJson);
            if (sections) {
                for (const auto &code : sections->answers)
                    m_questionnaireChoices.insert(QString::fromStdString(code),
                                                  {QStringLiteral("answer"), 3});
                for (const auto &code : sections->noChange)
                    m_questionnaireChoices.insert(QString::fromStdString(code),
                                                  {QStringLiteral("no_change"), 0});
                for (const auto &code : sections->skipped)
                    m_questionnaireChoices.insert(QString::fromStdString(code),
                                                  {QStringLiteral("skipped"), 0});
            }
        }
    }
    rebuildQuestionnaireRows();

    // 最近的验收（三档结果由验收页/复盘展示）
    QVariantList assessmentRows;
    for (const auto &assessment : assessmentRepo.listForUser(*userUid, 20)) {
        assessmentRows.append(QVariantMap{
            {QStringLiteral("uid"), QString::fromStdString(assessment.uid.value())},
            {QStringLiteral("title"), QString::fromStdString(assessment.assessmentType)},
            {QStringLiteral("subtitle"), QString::fromStdString(assessment.scopeJson)},
            {QStringLiteral("badge"), QString::fromStdString(Domain::toString(assessment.status))},
            {QStringLiteral("badgeTone"),
             assessment.status == Domain::AssessmentStatus::Scored
                 ? QStringLiteral("success")
                 : QStringLiteral("neutral")},
            {QStringLiteral("detail"), QString::fromStdString(assessment.generatedBy)}});
    }
    m_assessmentsModel.replace(assessmentRows);

    setState(QStringLiteral("ready"));
    emit reviewChanged();
    emit dataChanged();
}

void ReviewViewModel::rebuildQuestionnaireRows()
{
    QVariantList rows;
    for (const auto &code : m_questionnaireCodes) {
        const auto choice = m_questionnaireChoices.value(code);
        const QString choiceText =
            choice.first == QStringLiteral("answer")
                ? QStringLiteral("已回答 %1").arg(choice.second)
                : (choice.first == QStringLiteral("no_change") ? QStringLiteral("无明显变化")
                                                               : QStringLiteral("已跳过"));
        rows.append(QVariantMap{{QStringLiteral("uid"), code},
                                {QStringLiteral("title"), code},
                                {QStringLiteral("subtitle"),
                                 choice.first.isEmpty() ? QStringLiteral("未处理") : choiceText},
                                {QStringLiteral("badge"),
                                 choice.first.isEmpty() ? QStringLiteral("未处理") : choiceText},
                                {QStringLiteral("badgeTone"),
                                 QString::fromLatin1(kChoiceTone(choice.first))}});
    }
    m_questionnaireModel.replace(rows);
    emit dataChanged();
}

void ReviewViewModel::setQuestionnaireChoice(const QString &itemCode, const QString &choice,
                                             int value)
{
    if (!m_questionnaireCodes.contains(itemCode))
        return;
    if (choice != QStringLiteral("answer") && choice != QStringLiteral("no_change")
        && choice != QStringLiteral("skipped")) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("无效的问卷选项"));
        return;
    }
    m_questionnaireChoices.insert(itemCode, {choice, value});
    rebuildQuestionnaireRows();
}

void ReviewViewModel::submitQuestionnaire()
{
    if (m_reviewUid.isEmpty() || m_questionnaireUid.isEmpty()) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("当前没有可提交问卷的复盘"));
        return;
    }

    // 构造 responses_json：answers / no_change / skipped 三区块互斥
    QJsonObject answers;
    QJsonArray noChange;
    QJsonArray skipped;
    for (const auto &code : m_questionnaireCodes) {
        const auto choice = m_questionnaireChoices.value(code);
        if (choice.first == QStringLiteral("answer"))
            answers.insert(code, choice.second);
        else if (choice.first == QStringLiteral("no_change"))
            noChange.append(code);
        else if (choice.first == QStringLiteral("skipped"))
            skipped.append(code);
    }
    QJsonObject root;
    root.insert(QStringLiteral("answers"), answers);
    root.insert(QStringLiteral("no_change"), noChange);
    root.insert(QStringLiteral("skipped"), skipped);
    const std::string responsesJson =
        QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)).toStdString();

    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlReviewRepository reviewRepo(database, clock);
    Infrastructure::SqlQuestionnaireRepository questionnaireRepo(database, clock);
    Application::QuestionnaireUseCases useCases(questionnaireRepo, reviewRepo, uids, clock);

    const auto reviewUid = Domain::Uid::parse(m_reviewUid.toStdString());
    const auto questionnaireUid = Domain::Uid::parse(m_questionnaireUid.toStdString());
    if (!reviewUid || !questionnaireUid) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("无效的复盘或问卷标识"));
        return;
    }
    Application::QuestionnaireUseCases::SubmitInput input;
    input.responsesJson = responsesJson;
    const auto submitted = useCases.submitResponse(*reviewUid, *questionnaireUid, input);
    if (!submitted) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(submitted.error().message, submitted.error().detail));
        return;
    }
    refresh();
}

void ReviewViewModel::submitReview()
{
    if (m_reviewUid.isEmpty()) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("当前没有可提交的复盘"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlReviewRepository reviewRepo(database, clock);
    Application::ReviewUseCases useCases(reviewRepo, uids, clock);

    const auto current = reviewRepo.findByUid(*Domain::Uid::parse(m_reviewUid.toStdString()));
    if (!current) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("复盘不存在"));
        return;
    }
    Application::ReviewUseCases::SubmitInput input;
    input.summary = m_reviewSummary.trimmed().toStdString();
    input.userComment = {};
    input.nextAction = m_reviewNextActions.trimmed().toStdString();
    const auto submitted = useCases.submitReview(current->melId, current->revision, input);
    if (!submitted) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(submitted.error().message, submitted.error().detail));
        return;
    }
    refresh();
}

void ReviewViewModel::closeReview()
{
    if (m_reviewUid.isEmpty()) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("当前没有可关闭的复盘"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlReviewRepository reviewRepo(database, clock);
    Application::ReviewUseCases useCases(reviewRepo, uids, clock);

    const auto current = reviewRepo.findByUid(*Domain::Uid::parse(m_reviewUid.toStdString()));
    if (!current) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("复盘不存在"));
        return;
    }
    const auto closed = useCases.closeReview(current->melId, current->revision);
    if (!closed) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(closed.error().message, closed.error().detail));
        return;
    }
    refresh();
}

} // namespace PersonOS
