// 模块：聊天授权提取（R4.3.3；敏感状态最小收集）
// 覆盖：默认关闭 → Permission；开启后非敏感捕获（source=conversation、
//      语境片段限长、AI 推断置信 <1.0）；敏感定义需逐项确认；关闭后拒绝；
//      原始事件可查询且不保存整段对话。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "application/usecases/state/StateUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/knowledge/StateDefinitionsSeed.h"
#include "infrastructure/persistence/SqlStateRepository.h"

using namespace PersonOS;

namespace {
const char *kUserUid = "00000000-0000-0000-0000-0000000000aa";
} // namespace

class TstConversationState : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_conversation.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
        QVERIFY(Infrastructure::LearningManifestSeed(DatabaseManager::instance().database(),
                                                     m_clock)
                    .ensureSeeded());
        QVERIFY(Infrastructure::StateDefinitionsSeed(DatabaseManager::instance().database(),
                                                     m_clock)
                    .ensureSeeded());

        QSqlQuery user(DatabaseManager::instance().database());
        const bool userOk = user.exec(QStringLiteral(
            "INSERT INTO user_profiles_v3(uid,timezone_id,locale,onboarding_status,profile_json,"
            "created_at,updated_at) VALUES('00000000-0000-0000-0000-0000000000aa',"
            "'Asia/Shanghai','zh-CN','complete','{}','2026-09-27T00:00:00Z','2026-09-27T00:00:00Z')"));
        if (!userOk)
            QFAIL(qPrintable(QStringLiteral("user insert: %1").arg(user.lastError().text())));
    }

    void chatExtractionAuthorizationAndMinimalCollection()
    {
        Infrastructure::SqlStateRepository repo(DatabaseManager::instance().database(), m_clock);
        Application::StateUseCases useCases(repo, m_uids, m_clock);
        const auto userUid = *Domain::Uid::parse(kUserUid);

        // 默认关闭 → Permission
        QVERIFY(useCases.chatExtractionEnabled(userUid));
        QVERIFY(!useCases.chatExtractionEnabled(userUid).value());
        Application::StateUseCases::ConversationInput denied;
        denied.definitionCode = "energy";
        denied.valueJson = "{\"value\":3,\"min\":1,\"max\":5}";
        denied.conversationRef = "用户说今天状态一般";
        const auto deniedResult = useCases.captureFromConversation(userUid, denied);
        QVERIFY(!deniedResult);
        QVERIFY(deniedResult.error().code == Application::ErrorCode::Permission);

        // 开启授权
        QVERIFY(useCases.setChatExtractionEnabled(userUid, true));
        QVERIFY(useCases.chatExtractionEnabled(userUid).value());

        // 非敏感捕获（energy = normal）：source=conversation，语境片段保留
        const auto captured = useCases.captureFromConversation(userUid, denied);
        if (!captured)
            QFAIL(qPrintable(QString::fromStdString(captured.error().message + ": "
                                                    + captured.error().detail)));
        QVERIFY(captured.value().source == Domain::StateSource::Conversation);
        QVERIFY(captured.value().conversationRef.has_value());
        QVERIFY(captured.value().confidence < 1.0);

        // 置信度 1.0 → Validation（AI 推断不得冒充确定事实）
        Application::StateUseCases::ConversationInput certain = denied;
        certain.confidence = 1.0;
        QVERIFY(!useCases.captureFromConversation(userUid, certain));

        // 敏感定义（stress = sensitive）未确认 → Permission；确认后成功
        Application::StateUseCases::ConversationInput sensitive;
        sensitive.definitionCode = "stress";
        sensitive.valueJson = "{\"value\":4,\"min\":1,\"max\":5}";
        sensitive.conversationRef = "用户提到最近压力较大";
        const auto unconfirmed = useCases.captureFromConversation(userUid, sensitive);
        QVERIFY(!unconfirmed);
        QVERIFY(unconfirmed.error().code == Application::ErrorCode::Permission);
        sensitive.confirmedByUser = true;
        const auto confirmed = useCases.captureFromConversation(userUid, sensitive);
        QVERIFY(confirmed);
        QVERIFY(confirmed.value().consentScope == "explicit_confirmation");

        // 最小收集：语境片段限长 500 字符；事件不保存整段对话
        Application::StateUseCases::ConversationInput longContext = denied;
        longContext.conversationRef = std::string(1200, 'x');
        const auto longCaptured = useCases.captureFromConversation(userUid, longContext);
        QVERIFY(longCaptured);
        QVERIFY(longCaptured.value().conversationRef->size() <= 500);

        // 关闭授权 → 再拒绝
        QVERIFY(useCases.setChatExtractionEnabled(userUid, false));
        QVERIFY(!useCases.chatExtractionEnabled(userUid).value());
        const auto afterDisable = useCases.captureFromConversation(userUid, denied);
        QVERIFY(!afterDisable);
        QVERIFY(afterDisable.error().code == Application::ErrorCode::Permission);

        // 事件落库且 source=conversation 可查询（与显式记录分离）
        QSqlQuery count(DatabaseManager::instance().database());
        QVERIFY(count.exec(QStringLiteral(
            "SELECT COUNT(*) FROM state_events_v4 WHERE source='conversation'")));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), 3);   // energy + stress + 长语境 energy
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstConversationState)
#include "tst_conversation_state.moc"
