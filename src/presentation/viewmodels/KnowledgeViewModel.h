#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <vector>

#include "domain/knowledge/Knowledge.h"
#include "presentation/viewmodels/ListItemModel.h"

// 知识库页 ViewModel（DD-001 §11；五库浏览只读投影 + 用户导入命令）
// 五库共享骨架：论文/方案/方法/贴士/协议以类型徽标区分；来源与版本可追溯。
namespace PersonOS {

class KnowledgeViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pageState READ pageState NOTIFY pageStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QString currentKind READ currentKind NOTIFY kindChanged)
    Q_PROPERTY(QStringList domainSuggestions READ domainSuggestions NOTIFY domainSuggestionsChanged)
    Q_PROPERTY(ListItemModel *itemsModel READ itemsModel CONSTANT)
    Q_PROPERTY(bool detailVisible READ detailVisible NOTIFY detailChanged)
    Q_PROPERTY(QString detailTitle READ detailTitle NOTIFY detailChanged)
    Q_PROPERTY(QString detailMeta READ detailMeta NOTIFY detailChanged)
    Q_PROPERTY(QString detailSummary READ detailSummary NOTIFY detailChanged)
    Q_PROPERTY(QStringList detailClaims READ detailClaims NOTIFY detailChanged)
    Q_PROPERTY(QStringList detailApplicability READ detailApplicability NOTIFY detailChanged)
    Q_PROPERTY(QStringList detailLimitations READ detailLimitations NOTIFY detailChanged)
    Q_PROPERTY(QStringList detailSteps READ detailSteps NOTIFY detailChanged)
    Q_PROPERTY(QStringList detailEvidence READ detailEvidence NOTIFY detailChanged)
    QML_ELEMENT

public:
    explicit KnowledgeViewModel(QObject *parent = nullptr);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void search(const QString &text);
    // 手动添加一条方法/贴士（用户实践方法进入方法库前的最低形式）
    Q_INVOKABLE void addItem(const QString &kind, const QString &title, const QString &summary);
    // 论文经单文献处理核心导入(PDF/文本):提取、指纹去重、受管原件、入库;
    // domainSlug 形如 01-goal-self-regulation(空 = 不分类不编号)
    Q_INVOKABLE void importPaperFile(const QString &fileUrl, const QString &domainSlug);
    // 文献转方法(R4.4.1):受管原件全文提取 → AI 分析 → 候选方法/贴士
    // 入库(证据与来源关联,可撤销);后台线程执行,完成后刷新列表
    Q_INVOKABLE void convertPaperToMethods(const QString &itemUid);
    // 详情视图(摘要/主张/适用/局限/步骤/证据)
    Q_INVOKABLE void openItem(const QString &itemUid);
    Q_INVOKABLE void closeDetail();
    // 物理删除 AI 生成候选(2026-09-29 用户决策;论文/用户条目不可删)
    Q_INVOKABLE void removeGeneratedItem(const QString &itemUid);
    // 分类浏览(五库分层):空 = 全部,否则 paper/plan/method/tip
    Q_INVOKABLE void setKind(const QString &kind);
    // 本地向量分类推荐(零成本):按文件路径取文件名,后台线程返回 top-3
    Q_INVOKABLE void requestDomainSuggestions(const QString &filePath);

    QString pageState() const { return m_pageState; }
    QString lastError() const { return m_lastError; }
    QString notice() const { return m_notice; }
    QString currentKind() const { return m_currentKind; }
    QStringList domainSuggestions() const { return m_domainSuggestions; }
    ListItemModel *itemsModel() { return &m_itemsModel; }
    bool detailVisible() const { return m_detailVisible; }
    QString detailTitle() const { return m_detailTitle; }
    QString detailMeta() const { return m_detailMeta; }
    QString detailSummary() const { return m_detailSummary; }
    QStringList detailClaims() const { return m_detailClaims; }
    QStringList detailApplicability() const { return m_detailApplicability; }
    QStringList detailLimitations() const { return m_detailLimitations; }
    QStringList detailSteps() const { return m_detailSteps; }
    QStringList detailEvidence() const { return m_detailEvidence; }

signals:
    void pageStateChanged();
    void lastErrorChanged();
    void dataChanged();
    void noticeChanged();
    void detailChanged();
    void kindChanged();
    void domainSuggestionsChanged();

private:
    void setState(const QString &state);
    void setError(const QString &message);
    void setNotice(const QString &message);
    void showItems(const std::vector<Domain::KnowledgeItem> &items);
    void applyFilter();

    QString m_pageState = QStringLiteral("idle");
    QString m_lastError;
    QString m_notice;
    QString m_currentKind;
    QStringList m_domainSuggestions;
    QVariantList m_allRows;
    ListItemModel m_itemsModel;
    bool m_detailVisible = false;
    QString m_detailTitle;
    QString m_detailMeta;
    QString m_detailSummary;
    QStringList m_detailClaims;
    QStringList m_detailApplicability;
    QStringList m_detailLimitations;
    QStringList m_detailSteps;
    QStringList m_detailEvidence;
};

} // namespace PersonOS
