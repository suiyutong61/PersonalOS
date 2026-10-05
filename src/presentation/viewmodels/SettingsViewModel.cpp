#include "presentation/viewmodels/SettingsViewModel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSqlDatabase>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrent>

#include "application/audit/Audit.h"
#include "application/usecases/operations/OperationUseCases.h"
#include "database/DatabaseManager.h"
#include "domain/foundation/Uid.h"
#include "infrastructure/ai/SqlAiRepository.h"
#include "infrastructure/ai/OpenAiCompatibleProvider.h"
#include "infrastructure/ai/ModelConnectionTestService.h"
#include "infrastructure/ai/WindowsCredentialStore.h"
#include "infrastructure/embedding/DomainClassifier.h"
#include "infrastructure/embedding/LocalEmbeddingProvider.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/EmbeddingBackfill.h"
#include "infrastructure/knowledge/SqlEmbeddingRepository.h"
#include "infrastructure/operations/DatabaseManagerSwitch.h"
#include "infrastructure/operations/SqlOperationsRepository.h"
#include "infrastructure/operations/SqliteBackup.h"
#include "infrastructure/persistence/DatabaseConnectionFactory.h"
#include "infrastructure/persistence/UserProfileBootstrap.h"
#include "presentation/viewmodels/VmSupport.h"

namespace PersonOS {

namespace {

QVariantMap connectionRow(const Domain::AiProviderConfig &config,
                          const std::optional<Domain::AiConnectionTest> &test)
{
    return {{QStringLiteral("uid"), QString::fromStdString(config.uid.value())},
            {QStringLiteral("title"), QString::fromStdString(config.displayName)},
            {QStringLiteral("subtitle"),
             Presentation::providerCodeLabel(QString::fromStdString(config.providerCode))
                 + QStringLiteral(" · ")
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
            {QStringLiteral("title"),
             Presentation::displayDateTime(QString::fromStdString(record.startedAt))},
            {QStringLiteral("subtitle"), QString::fromStdString(record.relativePath)},
            {QStringLiteral("badge"),
             record.status == "verified" ? QStringLiteral("已校验")
                                         : QString::fromStdString(record.status)},
            {QStringLiteral("badgeTone"),
             record.status == "verified" ? QStringLiteral("success") : QStringLiteral("neutral")},
            // 只显示校验值前缀(完整值用于恢复校验,不用于展示)
            {QStringLiteral("detail"),
             record.sha256 ? QString::fromStdString(*record.sha256).left(12)
                                 + QStringLiteral("…")
                           : QString()}};
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

void SettingsViewModel::setNotice(const QString &message)
{
    m_notice = message;
    emit noticeChanged();
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
    m_remindersEnabled = Presentation::remindersGloballyEnabled();
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

void SettingsViewModel::setRemindersEnabled(bool enabled)
{
    m_remindersEnabled = enabled;
    Presentation::setRemindersGloballyEnabled(enabled);
    setNotice(enabled ? QStringLiteral("已启用应用内提醒")
                      : QStringLiteral("已关闭整体提醒：规则与记录保留，投递暂停，重新开启后恢复"));
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
    // 探测到的能力回写配置（修订号守卫，重读当前行）：
    // 能力路由与 HTTP 向量通道依赖 config.capabilities_json，
    // 此前从不回写导致 embedding 能力永不生效
    {
        const auto current = repo.findConfig(*parsed);
        if (!current) {
            setState(QStringLiteral("error"));
            setError(QStringLiteral("连接不存在"));
            return;
        }
        auto updated = *current;
        updated.capabilitiesJson = tested.value().capabilitiesJson;
        const auto saved = repo.updateConfig(updated, current->revision);
        if (!saved.ok) {
            setState(QStringLiteral("conflict"));
            setError(QStringLiteral("配置已被修改，请重新测试"));
            return;
        }
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

void SettingsViewModel::saveToolPaths(const QString &pdftotext, const QString &tesseract)
{
    QSettings settings;
    if (!pdftotext.trimmed().isEmpty())
        settings.setValue(QStringLiteral("literature/pdftotext"), pdftotext.trimmed());
    else
        settings.remove(QStringLiteral("literature/pdftotext"));
    if (!tesseract.trimmed().isEmpty())
        settings.setValue(QStringLiteral("literature/tesseract"), tesseract.trimmed());
    else
        settings.remove(QStringLiteral("literature/tesseract"));
    setError({});
    setNotice(QStringLiteral("工具路径已保存"));
}

QString SettingsViewModel::currentPdfToTextPath()
{
    return Presentation::configuredPdfToTextPath();
}

QString SettingsViewModel::currentTesseractPath()
{
    return Presentation::configuredTesseractPath();
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

void SettingsViewModel::verifyBackup(const QString &uid)
{
    const auto parsed = Domain::Uid::parse(uid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("无效的备份标识"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlOperationsRepository repo(database, clock);
    Infrastructure::SqliteBackup snapshots;
    Application::BackupService backups(repo, snapshots, uids, clock);
    const auto verified = backups.verifyBackup(*parsed);
    if (!verified) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(verified.error().message,
                                             verified.error().detail));
        return;
    }
    setNotice(verified.value() ? QStringLiteral("备份校验通过：内容与校验值一致")
                               : QStringLiteral("备份校验失败：内容与校验值不一致（可能被篡改）"));
    refresh();
}

void SettingsViewModel::restoreBackup(const QString &uid)
{
    const auto parsed = Domain::Uid::parse(uid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("无效的备份标识"));
        return;
    }
    setState(QStringLiteral("loading"));
    setError({});
    setNotice({});
    // 受控点:切换前关闭本线程的审计工厂连接(其文件句柄指向旧库,
    // 切换后旧文件被改名,继续使用会把审计写进恢复前快照)
    Infrastructure::DatabaseConnectionFactory::closeCurrentThreadConnection(
        QStringLiteral("audit"));

    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Application::RestoreService::RestoreReport report;
    {
        Infrastructure::SqlOperationsRepository repo(DatabaseManager::instance().database(),
                                                     clock);
        Infrastructure::DatabaseManagerSwitch switcher;
        Application::RestoreService restores(repo, switcher, uids, clock);
        const auto restored = restores.restore(*parsed,
                                               DatabaseManager::instance().schemaVersion());
        if (!restored) {
            setState(QStringLiteral("error"));
            setError(Presentation::friendlyError(restored.error().message,
                                                 restored.error().detail));
            return;
        }
        report = restored.value();
    }

    // 恢复成功后(新连接已重开):重新执行启动引导,否则恢复的库若缺少
    // 档案/种子,依赖它们的页面全部不可用(真机隐患排查发现的首发引导缺口)
    if (!Infrastructure::ensureStartupSeeded(DatabaseManager::instance().database()))
        qWarning("恢复后启动引导未全部完成");
    // 领域向量内存缓存来自旧库:失效后按指纹重新装载/重算
    Infrastructure::DomainClassifier::instance().invalidateCache();
    // 补记备份状态与恢复审计(旧连接已失效,必须用新连接)
    {
        Infrastructure::SqlOperationsRepository repo(DatabaseManager::instance().database(),
                                                     clock);
        const auto current = repo.find(*parsed);
        if (current) {
            auto updated = *current;
            updated.status = "verified";   // 状态枚举固定 running/verified/failed
            updated.completedAt = clock.utcIso();
            const auto saved = repo.update(updated);
            if (!saved.ok)
                qWarning("恢复后备份状态补记失败");
        }
        Application::Audit::record(
            {"user", {}, "backup.restored", "backup", uid.toStdString(),
             "{\"pre_restore_snapshot\":\"" + report.preRestoreSnapshotPath + "\"}"});
    }
    setNotice(QStringLiteral("已恢复备份，恢复前数据保留在快照：%1")
                  .arg(QString::fromStdString(report.preRestoreSnapshotPath)));
    refresh();
}

void SettingsViewModel::rebuildVectorIndex()
{
    if (!Infrastructure::LocalEmbeddingProvider::filesPresent()) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("本地向量模型未安装（embedding/model.onnx 缺失），无法重建向量索引"));
        return;
    }
    setState(QStringLiteral("loading"));
    setError({});
    setNotice({});
    const QString dbPath = DatabaseManager::instance().databasePath();
    QtConcurrent::run([dbPath]() {
        Infrastructure::LocalEmbeddingProvider &provider =
            Infrastructure::sharedLocalEmbedding();
        if (!provider.isReady())
            return QStringLiteral("本地向量模型加载失败：") + provider.lastError();
        Infrastructure::DatabaseConnectionFactory factory(dbPath);
        QString openError;
        QSqlDatabase workerDb =
            factory.openForCurrentThread(QStringLiteral("embedding_rebuild"), &openError);
        if (!workerDb.isOpen())
            return QStringLiteral("数据库连接失败：") + openError;
        const auto finish = [&](const QString &message) {
            Infrastructure::DatabaseConnectionFactory::closeCurrentThreadConnection(
                QStringLiteral("embedding_rebuild"));
            return message;
        };
        {
            // 清空当前模型向量 → 全量回填（可重建派生数据，不影响原始知识）
            Infrastructure::SqlEmbeddingRepository store(workerDb);
            const auto removed = store.removeForModel(provider.modelId());
            if (!removed.hasValue())
                return finish(QStringLiteral("向量清理失败：")
                              + QString::fromStdString(removed.error().message));
            Infrastructure::EmbeddingBackfill backfill(workerDb, provider);
            const auto progress = backfill.runOneBatch(0);
            // 领域行立即重种：避免重建→重启前领域分类缺失的窗口
            Infrastructure::DomainClassifier::persistDomainsToDatabase(dbPath);
            return finish(QStringLiteral("OK:已重建 %1 条知识向量（%2 条失败）")
                              .arg(progress.embedded)
                              .arg(progress.failed));
        }
    }).then([this](const QString &resultText) {
        if (resultText.startsWith(QStringLiteral("OK:"))) {
            setNotice(resultText.mid(3));
            refresh();
        } else {
            setState(QStringLiteral("error"));
            setError(resultText);
        }
    });
}

} // namespace PersonOS
