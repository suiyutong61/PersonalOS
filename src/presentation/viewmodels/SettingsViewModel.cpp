#include "presentation/viewmodels/SettingsViewModel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include "application/usecases/operations/OperationUseCases.h"
#include "database/DatabaseManager.h"
#include "domain/foundation/Uid.h"
#include "infrastructure/ai/SqlAiRepository.h"
#include "infrastructure/ai/OpenAiCompatibleProvider.h"
#include "infrastructure/ai/ModelConnectionTestService.h"
#include "infrastructure/ai/WindowsCredentialStore.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/operations/SqlOperationsRepository.h"
#include "infrastructure/operations/SqliteBackup.h"
#include "presentation/viewmodels/VmSupport.h"

namespace PersonOS {

namespace {

QVariantMap connectionRow(const Domain::AiProviderConfig &config,
                          const std::optional<Domain::AiConnectionTest> &test)
{
    return {{QStringLiteral("uid"), QString::fromStdString(config.uid.value())},
            {QStringLiteral("title"), QString::fromStdString(config.displayName)},
            {QStringLiteral("subtitle"),
             QString::fromStdString(config.providerCode) + QStringLiteral(" · ")
                 + QString::fromStdString(config.model)},
            {QStringLiteral("detail"), QString::fromStdString(config.endpoint)
                 + QStringLiteral(" · ")
                 + (!test ? QStringLiteral("未测试")
                          : test->overallOk ? QStringLiteral("测试通过")
                                            : QStringLiteral("测试失败"))},
            {QStringLiteral("badge"),
             (config.isDefault ? QStringLiteral("默认 · ") : QString())
                 + (config.enabled ? QStringLiteral("已启用")
                                   : QStringLiteral("已停用"))},
            {QStringLiteral("badgeTone"), config.enabled ? QStringLiteral("success")
                                                         : QStringLiteral("neutral")},
            {QStringLiteral("isDefault"), config.isDefault}};
}

QVariantMap backupRow(const Domain::BackupRecord &record)
{
    return {{QStringLiteral("uid"), QString::fromStdString(record.uid.value())},
            {QStringLiteral("title"), QString::fromStdString(record.startedAt)},
            {QStringLiteral("subtitle"), QString::fromStdString(record.relativePath)},
            {QStringLiteral("badge"), QString::fromStdString(record.status)},
            {QStringLiteral("badgeTone"),
             record.status == "verified" ? QStringLiteral("success") : QStringLiteral("neutral")},
            {QStringLiteral("detail"),
             record.sha256 ? QString::fromStdString(*record.sha256) : QString()}};
}

} // namespace

SettingsViewModel::SettingsViewModel(QObject *parent) : QObject(parent) {}

void SettingsViewModel::setState(const QString &state)
{
    if (m_pageState == state)
        return;
    m_pageState = state;
    emit pageStateChanged();
}

void SettingsViewModel::setError(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
}

void SettingsViewModel::setDarkMode(bool value)
{
    if (m_darkMode == value)
        return;
    m_darkMode = value;
    emit themeChanged();
}

void SettingsViewModel::setReducedMotion(bool value)
{
    if (m_reducedMotion == value)
        return;
    m_reducedMotion = value;
    emit themeChanged();
}

void SettingsViewModel::refresh()
{
    setState(QStringLiteral("loading"));
    setError({});
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlAiRepository aiRepo(database, clock);
    Infrastructure::SqlOperationsRepository opsRepo(database, clock);

    QVariantList connectionRows;
    for (const auto &config : aiRepo.listConfigs())
        connectionRows.append(connectionRow(config, aiRepo.latestConnectionTest(config.uid)));
    m_connectionsModel.replace(connectionRows);

    QVariantList backupRows;
    for (const auto &record : opsRepo.list(20))
        backupRows.append(backupRow(record));
    m_backupsModel.replace(backupRows);

    setState(QStringLiteral("ready"));
    emit dataChanged();
}

void SettingsViewModel::saveConnection(const QString &displayName, const QString &endpoint,
                                       const QString &model, const QString &apiKey)
{
    if (displayName.trimmed().isEmpty() || endpoint.trimmed().isEmpty()
        || model.trimmed().isEmpty()) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("连接名称、API 基础地址与模型标识为必填"));
        return;
    }
    if (apiKey.isEmpty()) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("API Key 为必填（多数模型 API 不使用用户名密码）"));
        return;
    }

    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlAiRepository repo(database, clock);

    // 凭据先入系统凭据库（Windows Credential Manager），SQLite 只存引用
    Infrastructure::WindowsCredentialStore credentials;
    const std::string credentialRef =
        std::string("personos:ai:") + uids.next().value();
    const auto written = credentials.write(credentialRef, apiKey.toStdString());
    if (!written) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("凭据写入系统凭据库失败，API Key 未保存到本地数据库"));
        return;
    }

    // 编辑既有连接:更新名称/地址/模型,修改后强制停用并要求重新测试
    // (scope-v1 4.3:修改地址、模型或凭据后连接状态变为待测试)
    if (!m_editingUid.isEmpty()) {
        const auto parsedEdit = Domain::Uid::parse(m_editingUid.toStdString());
        if (!parsedEdit) {
            setState(QStringLiteral("error"));
            setError(QStringLiteral("无效的连接标识"));
            return;
        }
        const auto current = repo.findConfig(*parsedEdit);
        if (!current) {
            setState(QStringLiteral("error"));
            setError(QStringLiteral("连接不存在"));
            return;
        }
        if (!apiKey.isEmpty()) {
            const auto written = credentials.write(current->credentialRef,
                                                   apiKey.toStdString());
            if (!written) {
                setState(QStringLiteral("error"));
                setError(QStringLiteral("凭据写入系统凭据库失败"));
                return;
            }
        }
        auto updated = *current;
        updated.displayName = displayName.trimmed().toStdString();
        updated.endpoint = endpoint.trimmed().toStdString();
        updated.model = model.trimmed().toStdString();
        updated.enabled = false;   // 修改后必须重新测试再启用
        const auto saved = repo.updateConfig(updated, current->revision);
        if (!saved.ok) {
            setState(QStringLiteral("error"));
            setError(Presentation::friendlyError(saved.error.message, saved.error.detail));
            return;
        }
        cancelEdit();
        refresh();
        return;
    }

    Domain::AiProviderConfig config;
    config.uid = uids.next();
    config.providerCode = "openai_compatible";
    config.displayName = displayName.trimmed().toStdString();
    config.endpoint = endpoint.trimmed().toStdString();
    config.model = model.trimmed().toStdString();
    config.credentialRef = credentialRef;
    config.capabilitiesJson = std::string("{\"text\":true}");
    config.enabled = false;   // 必须先在设置页测试通过再启用（DR-025）
    const auto saved = repo.insertConfig(config);
    if (!saved.ok) {
        credentials.remove(credentialRef);
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(saved.error.message, saved.error.detail));
        return;
    }
    refresh();
}

void SettingsViewModel::startEdit(const QString &uid)
{
    const auto parsed = Domain::Uid::parse(uid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("无效的连接标识"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlAiRepository repo(database, clock);
    const auto current = repo.findConfig(*parsed);
    if (!current) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("连接不存在"));
        return;
    }
    m_editingUid = QString::fromStdString(current->uid.value());
    m_editingName = QString::fromStdString(current->displayName);
    m_editingEndpoint = QString::fromStdString(current->endpoint);
    m_editingModel = QString::fromStdString(current->model);
    setError({});
    emit editingChanged();
}

void SettingsViewModel::cancelEdit()
{
    m_editingUid.clear();
    m_editingName.clear();
    m_editingEndpoint.clear();
    m_editingModel.clear();
    emit editingChanged();
}

void SettingsViewModel::setDefaultConnection(const QString &uid)
{
    const auto parsed = Domain::Uid::parse(uid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("无效的连接标识"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlAiRepository repo(database, clock);
    const auto saved = repo.setDefaultConfig(*parsed);
    if (!saved.ok) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(saved.error.message, saved.error.detail));
        return;
    }
    refresh();
}

void SettingsViewModel::setConnectionEnabled(const QString &uid, bool enabled)
{
    const auto parsed = Domain::Uid::parse(uid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("无效的连接标识"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlAiRepository repo(database, clock);
    const auto current = repo.findConfig(*parsed);
    if (!current) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("连接不存在"));
        return;
    }
    auto updated = *current;
    if (enabled) {
        const auto test = repo.latestConnectionTest(*parsed);
        if (!test || !test->overallOk || test->providerModel != current->model) {
            setState(QStringLiteral("conflict"));
            setError(QStringLiteral("必须先完成当前模型的连接测试，且认证、结构化输出和所需能力全部通过"));
            return;
        }
    }
    updated.enabled = enabled;
    const auto saved = repo.updateConfig(updated, current->revision);
    if (!saved.ok) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(saved.error.message, saved.error.detail));
        return;
    }
    refresh();
}

void SettingsViewModel::testConnection(const QString &uid)
{
    const auto parsed = Domain::Uid::parse(uid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("无效的连接标识"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlAiRepository repo(database, clock);
    Infrastructure::WindowsCredentialStore credentials;
    Infrastructure::OpenAiCompatibleProvider provider(credentials);
    Infrastructure::ModelConnectionTestService service(repo, provider, &provider, uids, clock);
    setState(QStringLiteral("loading"));
    const auto tested = service.test(*parsed);
    if (!tested) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(tested.error().message, tested.error().detail));
        return;
    }
    if (!tested.value().overallOk) {
        setState(QStringLiteral("conflict"));
        // 把探针失败的具体原因带给用户（如“需要认证”），而不是只显示笼统失败
        const auto errors = QJsonDocument::fromJson(
            QByteArray::fromStdString(tested.value().errorJson)).object();
        QString reason = errors.value(QStringLiteral("text")).toString();
        if (reason.isEmpty() && tested.value().embeddingRequired && !tested.value().embeddingOk)
            reason = QStringLiteral("向量能力探测失败");
        if (!reason.isEmpty())
            reason = reason.left(160);
        setError(reason.isEmpty()
                     ? QStringLiteral("连接测试失败；配置保持停用")
                     : QStringLiteral("连接测试失败（%1）；配置保持停用").arg(reason));
        return;
    }
    refresh();
}

void SettingsViewModel::removeConnection(const QString &uid)
{
    const auto parsed = Domain::Uid::parse(uid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("无效的连接标识"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlAiRepository repo(database, clock);
    const auto current = repo.findConfig(*parsed);
    if (!current) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("连接不存在"));
        return;
    }
    if (current->enabled) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("已启用的连接不能删除，请先停用"));
        return;
    }
    const auto removed = repo.deleteConfig(*parsed);
    if (!removed.ok) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(removed.error.message, removed.error.detail));
        return;
    }
    // 连接与测试事实已删除，同时清理系统凭据库中的密钥（尽力而为）
    Infrastructure::WindowsCredentialStore credentials;
    credentials.remove(current->credentialRef);
    refresh();
}

void SettingsViewModel::createBackup(const QString &targetPath)
{
    if (targetPath.trimmed().isEmpty()) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("请选择备份保存路径"));
        return;
    }

    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlOperationsRepository repo(database, clock);
    Infrastructure::SqliteBackup snapshots;
    Application::BackupService backups(repo, snapshots, uids, clock);

    Application::BackupService::CreateInput input;
    input.sourceDbPath = DatabaseManager::instance().databasePath().toStdString();
    input.targetPath = targetPath.trimmed().toStdString();
    input.dbSchemaVersion = DatabaseManager::instance().schemaVersion();
    const auto created = backups.createBackup(input);
    if (!created) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(created.error().message, created.error().detail));
        return;
    }
    refresh();
}

} // namespace PersonOS
