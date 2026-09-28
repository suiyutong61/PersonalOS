#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "presentation/viewmodels/ListItemModel.h"

// 验收与复盘页 ViewModel（DD-001 §11；requirements R3.4/R3.5、DR-022/036）
// 复盘问卷可跳过、可修改；跳过/未回答不视为"无明显变化"确认；
// 三档掌握结果只描述本次检测表现，不宣称永久掌握。
namespace PersonOS {

class ReviewViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pageState READ pageState NOTIFY pageStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(ListItemModel *questionnaireModel READ questionnaireModel CONSTANT)
    Q_PROPERTY(ListItemModel *assessmentsModel READ assessmentsModel CONSTANT)
    Q_PROPERTY(QString reviewSummary READ reviewSummary WRITE setReviewSummary
                   NOTIFY reviewChanged)
    Q_PROPERTY(QString reviewProblems READ reviewProblems WRITE setReviewProblems
                   NOTIFY reviewChanged)
    Q_PROPERTY(QString reviewNextActions READ reviewNextActions WRITE setReviewNextActions
                   NOTIFY reviewChanged)
    Q_PROPERTY(QString reviewState READ reviewState NOTIFY reviewChanged)
    QML_ELEMENT

public:
    explicit ReviewViewModel(QObject *parent = nullptr);

    Q_INVOKABLE void refresh();
    // 问卷项三态：answer(回答)/no_change(无明显变化)/skipped(跳过)；value 为 1–5 量表
    Q_INVOKABLE void setQuestionnaireChoice(const QString &itemCode, const QString &choice,
                                            int value);
    Q_INVOKABLE void submitQuestionnaire();
    Q_INVOKABLE void submitReview();
    Q_INVOKABLE void closeReview();

    QString pageState() const { return m_pageState; }
    QString lastError() const { return m_lastError; }
    ListItemModel *questionnaireModel() { return &m_questionnaireModel; }
    ListItemModel *assessmentsModel() { return &m_assessmentsModel; }
    QString reviewSummary() const { return m_reviewSummary; }
    QString reviewProblems() const { return m_reviewProblems; }
    QString reviewNextActions() const { return m_reviewNextActions; }
    QString reviewState() const { return m_reviewState; }

    void setReviewSummary(const QString &value);
    void setReviewProblems(const QString &value);
    void setReviewNextActions(const QString &value);

signals:
    void pageStateChanged();
    void lastErrorChanged();
    void reviewChanged();
    void dataChanged();

private:
    void setState(const QString &state);
    void setError(const QString &message);
    void rebuildQuestionnaireRows();

    QString m_pageState = QStringLiteral("idle");
    QString m_lastError;
    QString m_reviewUid;
    QString m_questionnaireUid;
    QString m_reviewState;
    QString m_reviewSummary;
    QString m_reviewProblems;
    QString m_reviewNextActions;
    // itemCode → {choice, value}
    QHash<QString, QPair<QString, int>> m_questionnaireChoices;
    QStringList m_questionnaireCodes;
    ListItemModel m_questionnaireModel;
    ListItemModel m_assessmentsModel;
};

} // namespace PersonOS
