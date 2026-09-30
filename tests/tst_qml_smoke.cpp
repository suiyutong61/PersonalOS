// IMP-008：QML 冒烟测试（requirements 10.8 UI 验收的一部分）
// 离屏加载九页 + 导航壳，验证类型注册、组件与页面无 QML 错误；
// 页面级 ViewModel 的加载行为通过临时数据库验证（空数据 → empty 状态不崩溃）。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QSignalSpy>

#include "database/DatabaseManager.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/knowledge/ReviewQuestionnaireSeed.h"
#include "infrastructure/knowledge/StateDefinitionsSeed.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "presentation/viewmodels/AppNotifier.h"

class TstQmlSmoke : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // 离屏渲染：验证 QML 加载，不弹出窗口
        qputenv("QT_QPA_PLATFORM", "offscreen");
        m_path = QDir::temp().filePath(QStringLiteral("personos_qml_smoke.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
        PersonOS::Infrastructure::QtSystemClock clock;
        QVERIFY(PersonOS::Infrastructure::LearningManifestSeed(
                    DatabaseManager::instance().database(), clock)
                    .ensureSeeded());
        QVERIFY(PersonOS::Infrastructure::StateDefinitionsSeed(
                    DatabaseManager::instance().database(), clock)
                    .ensureSeeded());
        QVERIFY(PersonOS::Infrastructure::ReviewQuestionnaireSeed(
                    DatabaseManager::instance().database(), clock)
                    .ensureSeeded());
    }

    void mainWindowLoads()
    {
        QQmlApplicationEngine engine;
        // 与 main.cpp 同款装配：应用内提醒横幅单例（Toast 依赖）；
        // 独立 URI PersonOS.App，避免与 QML 模块 URI 冲突
        qmlRegisterSingletonInstance("PersonOS.App", 1, 0, "AppNotifier",
                                     &PersonOS::Presentation::AppNotifier::instance());
        QSignalSpy failed(&engine, &QQmlApplicationEngine::objectCreationFailed);
        engine.loadFromModule("PersonOS", "Main");
        QTRY_COMPARE_WITH_TIMEOUT(engine.rootObjects().size(), 1, 5000);
        QCOMPARE(failed.count(), 0);
        QVERIFY(!engine.rootObjects().isEmpty());
        const QObject *window = engine.rootObjects().first();
        QVERIFY2(window, "Main window must be created");
    }

    void appNotifierSingletonDelivers()
    {
        auto &notifier = PersonOS::Presentation::AppNotifier::instance();
        const int before = notifier.sequence();
        // 通知端口语义：deliver 即显示应用内横幅（v1 唯一通道）
        QVERIFY(notifier.deliver("Personal OS 提醒", "「测试 MEL」已到 Deadline"));
        QCOMPARE(notifier.sequence(), before + 1);
        QVERIFY(notifier.text().contains(QStringLiteral("Personal OS 提醒")));
        QVERIFY(notifier.text().contains(QStringLiteral("已到 Deadline")));
        notifier.dismiss();
        QVERIFY(notifier.text().isEmpty());
    }

private:
    QString m_path;
};

QTEST_MAIN(TstQmlSmoke)
#include "tst_qml_smoke.moc"
