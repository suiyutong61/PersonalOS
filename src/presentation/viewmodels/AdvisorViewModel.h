#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "presentation/viewmodels/ListItemModel.h"

// AI 助手页 ViewModel（DD-001 §11；DR-015/027）
// 诚实降级：未配置可用模型连接 → offline 状态；已配置 → 登记持久化任务（queued），
// 页面显示 ai_waiting；任务的实际执行与知识校准端到端接线按决定归 IMP-005/006。
// 不伪造 AI 结果，不把排队任务显示为已完成。
namespace PersonOS {

class AdvisorViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pageState READ pageState NOTIFY pageStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(ListItemModel *historyModel READ historyModel CONSTANT)
    Q_PROPERTY(bool detailVisible READ detailVisible NOTIFY detailChanged)
    Q_PROPERTY(QString detailQuestion READ detailQuestion NOTIFY detailChanged)
    Q_PROPERTY(QString detailAnswer READ detailAnswer NOTIFY detailChanged)
    QML_ELEMENT

public:
    explicit AdvisorViewModel(QObject *parent = nullptr);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void send(const QString &question);
    // 取消最新排队/执行中的任务（协作式取消：已提交的正式数据不回滚为不存在）
    Q_INVOKABLE void cancelLatestPending();
    // 详情弹层：完整展示问题与回答(列表行只显示截断预览)
    Q_INVOKABLE void openDetail(const QString &uid);
    Q_INVOKABLE void closeDetail();
    // 物理删除回答(2026-09-29 用户决策):连同底层任务/调用记录删除,
    // 历史审计事件保留;删除前由页面二次确认
    Q_INVOKABLE void deleteAnswer(const QString &uid);

    QString pageState() const { return m_pageState; }
    QString lastError() const { return m_lastError; }
    ListItemModel *historyModel() { return &m_historyModel; }
    bool detailVisible() const { return m_detailVisible; }
    QString detailQuestion() const { return m_detailQuestion; }
    QString detailAnswer() const { return m_detailAnswer; }

signals:
    void pageStateChanged();
    void lastErrorChanged();
    void dataChanged();
    void detailChanged();

private:
    void setState(const QString &state);
    void setError(const QString &message);

    QString m_pageState = QStringLiteral("idle");
    QString m_lastError;
    ListItemModel m_historyModel;
    bool m_detailVisible = false;
    QString m_detailQuestion;
    QString m_detailAnswer;
};

} // namespace PersonOS
