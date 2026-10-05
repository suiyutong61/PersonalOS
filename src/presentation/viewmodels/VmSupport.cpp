#include "presentation/viewmodels/VmSupport.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSettings>
#include <QSqlQuery>
#include <QStandardPaths>

#include <cmath>

namespace PersonOS::Presentation {

namespace {

// 在系统 PATH 与常见安装位置中寻找可执行文件;返回空表示未找到
QString discoverTool(const char *envName, const QString &executable,
                     const QStringList &knownLocations)
{
    const QString configured = QSettings().value(
        QString::fromLatin1("literature/%1").arg(QLatin1String(envName))).toString();
    if (!configured.isEmpty() && QFileInfo::exists(configured))
        return configured;
    const QString inPath = QStandardPaths::findExecutable(executable);
    if (!inPath.isEmpty())
        return inPath;
    for (const QString &location : knownLocations)
        if (QFileInfo::exists(location))
            return location;
    return {};
}

} // namespace

void applyPdfToolPaths()
{
    const QString pdftotext = discoverTool(
        "pdftotext", QStringLiteral("pdftotext"),
        {QStringLiteral("D:/VibeCodingDevelop/git/Git/mingw64/bin/pdftotext.exe")});
    const QString pdftoppm = discoverTool(
        "pdftoppm", QStringLiteral("pdftoppm"),
        {QStringLiteral("C:/Users/HP/.cache/codex-runtimes/codex-primary-runtime/"
                        "dependencies/native/poppler/Library/bin/pdftoppm.exe"),
         QStringLiteral("C:/Program Files/poppler/Library/bin/pdftoppm.exe")});
    const QString tesseract = discoverTool(
        "tesseract", QStringLiteral("tesseract"),
        {QStringLiteral("C:/Program Files/Tesseract-OCR/tesseract.exe"),
         QStringLiteral("D:/Tesseract-OCR/tesseract.exe")});
    if (!pdftotext.isEmpty())
        qputenv("PERSONOS_PDFTOTEXT_PATH", pdftotext.toUtf8());
    if (!pdftoppm.isEmpty())
        qputenv("PERSONOS_PDFTOPPM_PATH", pdftoppm.toUtf8());
    if (!tesseract.isEmpty())
        qputenv("PERSONOS_TESSERACT_PATH", tesseract.toUtf8());
}

QString configuredPdfToTextPath()
{
    return QSettings().value(QStringLiteral("literature/pdftotext")).toString();
}

QString configuredTesseractPath()
{
    return QSettings().value(QStringLiteral("literature/tesseract")).toString();
}

bool remindersGloballyEnabled()
{
    return QSettings().value(QStringLiteral("reminders/global_enabled"), true).toBool();
}

void setRemindersGloballyEnabled(bool enabled)
{
    QSettings().setValue(QStringLiteral("reminders/global_enabled"), enabled);
}

std::optional<Domain::Uid> activeUserUid(QSqlDatabase database)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT uid FROM user_profiles_v3 ORDER BY id LIMIT 1")))
        return std::nullopt;
    if (!query.next())
        return std::nullopt;
    return Domain::Uid::parse(query.value(0).toString().toStdString());
}

QString friendlyError(const std::string &message, const std::string &detail)
{
    // 已知错误的友好中文映射（不让原始英文/数据库错误文本直接暴露给用户）
    static const QHash<QString, QString> knownMessages = {
        {QStringLiteral("provider config already exists"),
         QStringLiteral("同一服务商与模型组合已存在连接：请直接使用已有连接，"
                        "或改用其他模型标识")},
        {QStringLiteral("empty map cannot be confirmed"),
         QStringLiteral("内容地图还没有节点：请先在上方按行添加章/节节点，再确认")},
        {QStringLiteral("only draft map can be confirmed"),
         QStringLiteral("只有草稿状态的内容地图可以确认")},
    };
    const QString messageText = QString::fromStdString(message);
    if (const auto friendly = knownMessages.constFind(messageText);
        friendly != knownMessages.constEnd())
        return friendly.value();
    QString text = messageText;
    if (!detail.empty()) {
        const QString detailText = QString::fromStdString(detail);
        const int newline = detailText.indexOf(u'\n');
        text += QStringLiteral("（%1）").arg(detailText.left(newline >= 0 ? newline : 120));
    }
    return text;
}

QString percentText(double ratio)
{
    if (!std::isfinite(ratio) || ratio < 0.0)
        return QStringLiteral("0%");
    if (ratio > 1.0)
        ratio = 1.0;
    return QString::number(static_cast<int>(std::lround(ratio * 100.0)))
           + QStringLiteral("%");
}

namespace {

QString labelOrRaw(const QHash<QString, QString> &map, const QString &value)
{
    return map.value(value, value);
}

const QHash<QString, QString> &knowledgeTypes()
{
    static const QHash<QString, QString> map = {
        {"paper", QStringLiteral("论文")},   {"plan", QStringLiteral("方案")},
        {"state", QStringLiteral("状态")},   {"method", QStringLiteral("方法")},
        {"tip", QStringLiteral("贴士")},
    };
    return map;
}

const QHash<QString, QString> &knowledgeStatuses()
{
    static const QHash<QString, QString> map = {
        {"draft", QStringLiteral("草稿")},     {"candidate", QStringLiteral("候选")},
        {"active", QStringLiteral("生效")},    {"warned", QStringLiteral("警告")},
        {"superseded", QStringLiteral("被取代")},
        {"archived", QStringLiteral("已归档")},
    };
    return map;
}

const QHash<QString, QString> &melStates()
{
    static const QHash<QString, QString> map = {
        {"draft", QStringLiteral("草稿")},
        {"awaiting_confirmation", QStringLiteral("待确认")},
        {"active", QStringLiteral("进行中")},     {"paused", QStringLiteral("已暂停")},
        {"execution_complete", QStringLiteral("执行完成")},
        {"overdue", QStringLiteral("已逾期")},    {"settling", QStringLiteral("结算中")},
        {"awaiting_assessment", QStringLiteral("待验收")},
        {"reviewing", QStringLiteral("复盘阶段")},
        {"closed", QStringLiteral("已关闭")},     {"cancelled", QStringLiteral("已取消")},
    };
    return map;
}

const QHash<QString, QString> &melTaskStates()
{
    static const QHash<QString, QString> map = {
        {"pending", QStringLiteral("待执行")},    {"in_progress", QStringLiteral("进行中")},
        {"completed", QStringLiteral("已完成")},  {"skipped", QStringLiteral("已跳过")},
        {"failed", QStringLiteral("未完成")},
    };
    return map;
}

const QHash<QString, QString> &reviewStatuses()
{
    static const QHash<QString, QString> map = {
        {"draft", QStringLiteral("草稿")},        {"collecting", QStringLiteral("收集中")},
        {"assessing", QStringLiteral("评估中")},  {"decision", QStringLiteral("决策中")},
        {"confirmed", QStringLiteral("已确认")},  {"closed", QStringLiteral("已关闭")},
    };
    return map;
}

const QHash<QString, QString> &assessmentStatuses()
{
    static const QHash<QString, QString> map = {
        {"draft", QStringLiteral("草稿")},        {"ready", QStringLiteral("就绪")},
        {"in_progress", QStringLiteral("进行中")}, {"scored", QStringLiteral("已评分")},
        {"cancelled", QStringLiteral("已取消")},
    };
    return map;
}

const QHash<QString, QString> &assessmentTypes()
{
    static const QHash<QString, QString> map = {
        {"written", QStringLiteral("笔试")},      {"practice", QStringLiteral("实操")},
        {"self_assessment", QStringLiteral("自评")},
        {"oral", QStringLiteral("口试")},
    };
    return map;
}

const QHash<QString, QString> &routeStatuses()
{
    static const QHash<QString, QString> map = {
        {"draft", QStringLiteral("草稿")},        {"proposed", QStringLiteral("候选")},
        {"confirmed", QStringLiteral("已确认")},  {"active", QStringLiteral("进行中")},
        {"superseded", QStringLiteral("被取代")}, {"completed", QStringLiteral("已完成")},
    };
    return map;
}

const QHash<QString, QString> &contentMapStatuses()
{
    static const QHash<QString, QString> map = {
        {"draft", QStringLiteral("草稿")},        {"confirmed", QStringLiteral("已确认")},
        {"retired", QStringLiteral("已退役")},
    };
    return map;
}

const QHash<QString, QString> &sourceTypes()
{
    static const QHash<QString, QString> map = {
        {"user_document", QStringLiteral("用户文档")}, {"user_edit", QStringLiteral("用户编辑")},
        {"import", QStringLiteral("导入")},            {"generated", QStringLiteral("AI 生成")},
        {"manual", QStringLiteral("手动")},            {"doi", QStringLiteral("DOI 文献")},
    };
    return map;
}

const QHash<QString, QString> &sourceModes()
{
    static const QHash<QString, QString> map = {
        {"knowledge_grounded", QStringLiteral("有知识依据")},
        {"partially_grounded", QStringLiteral("部分有依据")},
        {"ungrounded", QStringLiteral("无知识依据")},
    };
    return map;
}

const QHash<QString, QString> &questionnaireItems()
{
    static const QHash<QString, QString> map = {
        {"energy", QStringLiteral("精力")},         {"focus", QStringLiteral("专注")},
        {"mood", QStringLiteral("情绪")},           {"fatigue", QStringLiteral("疲劳")},
        {"sleep_hours", QStringLiteral("睡眠时长")},
        {"available_time_min", QStringLiteral("可用时间")},
        {"cognitive_load", QStringLiteral("认知负荷")}, {"stress", QStringLiteral("压力")},
        {"motivation", QStringLiteral("动机")},     {"recreation_need", QStringLiteral("休息需要")},
        {"body_condition", QStringLiteral("身体状态")},
        {"environment_quality", QStringLiteral("环境质量")},
        {"social_load", QStringLiteral("社交负荷")},
        {"behavior_adherence", QStringLiteral("行为坚持")},
        {"system_feedback", QStringLiteral("系统反馈")},
        {"domain_progress", QStringLiteral("领域进展")},
    };
    return map;
}

const QHash<QString, QString> &achievementTypes()
{
    static const QHash<QString, QString> map = {
        {"mel_completed", QStringLiteral("完成 MEL")},
        {"stage_reached", QStringLiteral("到达阶段")},
        {"review_streak", QStringLiteral("连续复盘")},
    };
    return map;
}

const QHash<QString, QString> &providerCodes()
{
    static const QHash<QString, QString> map = {
        {"openai_compatible", QStringLiteral("OpenAI 兼容")},
    };
    return map;
}

const QHash<QString, QString> &stageMaterialChoices()
{
    static const QHash<QString, QString> map = {
        {"pending", QStringLiteral("待决定")},
        {"accepted", QStringLiteral("已采用")},
        {"rejected", QStringLiteral("已排除")},
    };
    return map;
}

const QHash<QString, QString> &reminderDeliveryStatuses()
{
    static const QHash<QString, QString> map = {
        {"pending", QStringLiteral("待投递")},
        {"delivered", QStringLiteral("已投递")},
        {"failed", QStringLiteral("投递失败")},
        {"suppressed", QStringLiteral("静默时段")},
        {"cancelled", QStringLiteral("已取消")},
    };
    return map;
}

const QHash<QString, QString> &progressSuggestionTypes()
{
    static const QHash<QString, QString> map = {
        {"method", QStringLiteral("方法绑定")},
        {"task", QStringLiteral("任务调整")},
        {"reschedule", QStringLiteral("换序建议")},
        {"note", QStringLiteral("说明")},
    };
    return map;
}

} // namespace

QString knowledgeTypeLabel(const QString &type)
{
    return labelOrRaw(knowledgeTypes(), type);
}

QString knowledgeStatusLabel(const QString &status)
{
    return labelOrRaw(knowledgeStatuses(), status);
}

QString knowledgeDomainLabel(const QString &domain)
{
    if (domain == QStringLiteral("learning"))
        return QStringLiteral("学习");
    if (domain == QStringLiteral("unclassified"))
        return QStringLiteral("未分类");
    if (domain.startsWith(QStringLiteral("research.")))
        return domain.mid(9);   // research.06-sleep → 06-sleep
    return domain;
}

QString melStateLabel(const QString &state)
{
    return labelOrRaw(melStates(), state);
}

QString melTaskStateLabel(const QString &state)
{
    return labelOrRaw(melTaskStates(), state);
}

QString reviewStatusLabel(const QString &status)
{
    return labelOrRaw(reviewStatuses(), status);
}

QString assessmentStatusLabel(const QString &status)
{
    return labelOrRaw(assessmentStatuses(), status);
}

QString assessmentTypeLabel(const QString &type)
{
    return labelOrRaw(assessmentTypes(), type);
}

QString routeStatusLabel(const QString &status)
{
    return labelOrRaw(routeStatuses(), status);
}

QString contentMapStatusLabel(const QString &status)
{
    return labelOrRaw(contentMapStatuses(), status);
}

QString sourceTypeLabel(const QString &type)
{
    return labelOrRaw(sourceTypes(), type);
}

QString sourceModeLabel(const QString &mode)
{
    return labelOrRaw(sourceModes(), mode);
}

QString questionnaireItemLabel(const QString &code)
{
    return labelOrRaw(questionnaireItems(), code);
}

QString achievementTypeLabel(const QString &type)
{
    return labelOrRaw(achievementTypes(), type);
}

QString providerCodeLabel(const QString &code)
{
    return labelOrRaw(providerCodes(), code);
}

QString stageMaterialChoiceLabel(const QString &choice)
{
    return labelOrRaw(stageMaterialChoices(), choice);
}

QString reminderDeliveryStatusLabel(const QString &status)
{
    return labelOrRaw(reminderDeliveryStatuses(), status);
}

QString progressSuggestionTypeLabel(const QString &type)
{
    return labelOrRaw(progressSuggestionTypes(), type);
}

QString displayDateTime(const QString &isoUtc)
{
    const QDateTime parsed = QDateTime::fromString(isoUtc, Qt::ISODate);
    if (!parsed.isValid())
        return isoUtc;
    return parsed.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

} // namespace PersonOS::Presentation
