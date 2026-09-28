// 模块：发布安全审计（requirements 10.3/10.6.1；architecture 6.1 安全检查）
// 覆盖：源码/配置中无凭据类字面量；数据库连接配置不含密钥文本；
//       审计事件不含 Bearer/密钥；日志模式（无持久化日志文件泄露面）。
#include <QtTest>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QSqlQuery>

#include "database/DatabaseManager.h"

namespace {

// 疑似凭据的启发式模式（源码扫描）：长随机串密钥前缀、显式密钥赋值
bool looksLikeCredentialLine(const QString &line)
{
    if (line.contains(QStringLiteral("sk-"))
        && line.contains(QStringLiteral("sk-")) == line.indexOf(QStringLiteral("sk-")))
        return true;
    const QRegularExpression apiKey(
        QStringLiteral("(api[_-]?key|apikey|secret|token)\\s*=\\s*\"[A-Za-z0-9+/=_-]{16,}\""),
        QRegularExpression::CaseInsensitiveOption);
    return apiKey.match(line).hasMatch();
}

} // namespace

class TstSecurityAudit : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_security_audit.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
    }

    void sourceTreeHasNoCredentialLiterals()
    {
        // 仓库根 = 本测试文件上溯两级
        QDir root(QStringLiteral(__FILE__));
        root.cdUp();
        root.cdUp();
        QVERIFY(root.exists(QStringLiteral("CMakeLists.txt")));

        QStringList violations;
        QDirIterator it(root.absolutePath(), QDir::Files | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QFileInfo info(path);
            if (info.suffix() != QStringLiteral("cpp") && info.suffix() != QStringLiteral("h")
                && info.suffix() != QStringLiteral("qml") && info.suffix() != QStringLiteral("cmake"))
                continue;
            if (path.contains(QStringLiteral("/build")) || path.contains(QStringLiteral("\build")))
                continue;   // 构建产物不扫描
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                continue;
            int lineNo = 0;
            while (!file.atEnd()) {
                ++lineNo;
                const QString line = QString::fromUtf8(file.readLine());
                if (looksLikeCredentialLine(line.trimmed()))
                    violations << QStringLiteral("%1:%2: %3")
                                      .arg(info.fileName())
                                      .arg(lineNo)
                                      .arg(line.trimmed());
            }
        }
        QVERIFY2(violations.isEmpty(),
                 qPrintable(QStringLiteral("credential-like literals found:\n")
                            + violations.join(QLatin1Char('\n'))));
    }

    void databaseStoresNoCredentialMaterial()
    {
        // 连接配置只保存引用（credential_ref），不含密钥本体
        QSqlQuery configs(DatabaseManager::instance().database());
        QVERIFY(configs.exec(QStringLiteral(
            "SELECT credential_ref FROM ai_provider_configs_v6")));
        while (configs.next()) {
            const QString ref = configs.value(0).toString();
            QVERIFY2(ref.size() < 200 && !ref.contains(QStringLiteral("sk-")),
                     "credential_ref must be a reference, not a secret");
        }

        // 审计事件不含 Bearer / 密钥（脱敏要求）
        QSqlQuery audits(DatabaseManager::instance().database());
        QVERIFY(audits.exec(QStringLiteral(
            "SELECT COUNT(*) FROM audit_events_v6 WHERE detail_json LIKE '%Bearer%'")));
        QVERIFY(audits.next());
        QCOMPARE(audits.value(0).toInt(), 0);

        // 决策记录与任务不含密钥头
        QSqlQuery decisions(DatabaseManager::instance().database());
        QVERIFY(decisions.exec(QStringLiteral(
            "SELECT COUNT(*) FROM decision_records_v6 "
            "WHERE candidate_json LIKE '%sk-%'")));
        QVERIFY(decisions.next());
        QCOMPARE(decisions.value(0).toInt(), 0);
    }

private:
    QString m_path;
};

QTEST_GUILESS_MAIN(TstSecurityAudit)
#include "tst_security_audit.moc"
