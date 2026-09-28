#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlQuery>

#include <future>
#include <thread>

#include "application/foundation/ApplicationError.h"
#include "domain/foundation/Uid.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/persistence/DatabaseConnectionFactory.h"
#include "infrastructure/persistence/SqlUnitOfWork.h"

using namespace PersonOS;

class TstFoundation : public QObject
{
    Q_OBJECT

private slots:
    void uidValidation()
    {
        const auto generated = Infrastructure::QtUidGenerator().next();
        QVERIFY(!generated.empty());
        QVERIFY(Domain::Uid::parse(generated.value()).has_value());
        QVERIFY(!Domain::Uid::parse("not-a-uuid").has_value());
    }

    void connectionAndTransaction()
    {
        const QString path = QDir::temp().filePath(QStringLiteral("personos_foundation.db"));
        QFile::remove(path);

        Infrastructure::DatabaseConnectionFactory factory(path);
        QString error;
        QSqlDatabase db = factory.openForCurrentThread(QStringLiteral("foundation_test"), &error);
        QVERIFY2(db.isOpen(), qPrintable(error));

        QSqlQuery create(db);
        QVERIFY(create.exec(QStringLiteral("CREATE TABLE sample(id INTEGER PRIMARY KEY, value TEXT)")));

        {
            Infrastructure::SqlUnitOfWork unit(db);
            QVERIFY(unit.begin());
            QSqlQuery insert(db);
            QVERIFY(insert.exec(QStringLiteral("INSERT INTO sample(value) VALUES('rolled back')")));
        }
        QSqlQuery countAfterRollback(db);
        QVERIFY(countAfterRollback.exec(QStringLiteral("SELECT COUNT(*) FROM sample")));
        QVERIFY(countAfterRollback.next());
        QCOMPARE(countAfterRollback.value(0).toInt(), 0);

        Infrastructure::SqlUnitOfWork unit(db);
        QVERIFY(unit.begin());
        QSqlQuery insert(db);
        QVERIFY(insert.exec(QStringLiteral("INSERT INTO sample(value) VALUES('committed')")));
        QVERIFY(unit.commit());

        QSqlQuery countAfterCommit(db);
        QVERIFY(countAfterCommit.exec(QStringLiteral("SELECT COUNT(*) FROM sample")));
        QVERIFY(countAfterCommit.next());
        QCOMPARE(countAfterCommit.value(0).toInt(), 1);

        db = QSqlDatabase();
        Infrastructure::DatabaseConnectionFactory::closeCurrentThreadConnection(
            QStringLiteral("foundation_test"));
        QFile::remove(path);
    }

    void eachThreadGetsItsOwnConnection()
    {
        const QString path = QDir::temp().filePath(QStringLiteral("personos_thread_connections.db"));
        QFile::remove(path);
        Infrastructure::DatabaseConnectionFactory factory(path);
        const QString mainName = Infrastructure::DatabaseConnectionFactory::connectionName(
            QStringLiteral("worker_test"));

        std::promise<QString> workerNamePromise;
        auto workerName = workerNamePromise.get_future();
        std::thread worker([&factory, &workerNamePromise]() {
            QString error;
            QSqlDatabase db = factory.openForCurrentThread(QStringLiteral("worker_test"), &error);
            const QString name = db.connectionName();
            db = QSqlDatabase();
            Infrastructure::DatabaseConnectionFactory::closeCurrentThreadConnection(
                QStringLiteral("worker_test"));
            workerNamePromise.set_value(error.isEmpty() ? name : QString());
        });
        worker.join();

        const QString workerConnectionName = workerName.get();
        QVERIFY(!workerConnectionName.isEmpty());
        QVERIFY(workerConnectionName != mainName);
        QVERIFY(mainName != Infrastructure::DatabaseConnectionFactory::connectionName(
                               QStringLiteral("other_purpose")));
        // Worker connection was closed on its owner thread. Opening on the main
        // thread must use a distinct name and remain valid.
        QString error;
        QSqlDatabase db = factory.openForCurrentThread(QStringLiteral("worker_test"), &error);
        QVERIFY2(db.isOpen(), qPrintable(error));
        QCOMPARE(db.connectionName(), mainName);
        db = QSqlDatabase();
        Infrastructure::DatabaseConnectionFactory::closeCurrentThreadConnection(
            QStringLiteral("worker_test"));
        QFile::remove(path);
    }
};

QTEST_GUILESS_MAIN(TstFoundation)
#include "tst_foundation.moc"
