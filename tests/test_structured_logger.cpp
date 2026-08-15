// ═══════════════════════════════════════════════════════════════════════════════
// Test: StructuredLogger — write/reopen/parse round-trip, category mapping,
// and safe no-op behavior before a session is opened.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "core/structured_logger.h"

class TestStructuredLogger : public QObject
{
    Q_OBJECT
private slots:
    void testLogBeforeOpenSessionIsSafeNoOp()
    {
        // StructuredLogger is a process-wide singleton, so this must run
        // before any test that calls openSession() — verifies log() is a
        // safe no-op (not a crash) while no session has been opened yet.
        StructuredLogger& logger = StructuredLogger::instance();
        QVERIFY(logger.currentLogFilePath().isEmpty());
        logger.log(StructuredLogger::Category::Safety, "Test", "should not crash");
        QVERIFY(logger.currentLogFilePath().isEmpty());
    }

    void testWriteReopenParseRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        StructuredLogger& logger = StructuredLogger::instance();
        QVERIFY(logger.openSession(dir.path()));
        QVERIFY(!logger.currentLogFilePath().isEmpty());

        logger.log(StructuredLogger::Category::Safety, "GantryAxisController",
                   "Homing timed out — home switch not reached.");
        logger.log(StructuredLogger::Category::Connection, "ConnectionService",
                   "Dashboard disconnected");
        logger.log(StructuredLogger::Category::Motion, "PlaybackEngine",
                   "Keyframe fired: seg-42");
        // Deliberately includes quotes/backslash to exercise JSON escaping.
        logger.log(StructuredLogger::Category::Error, "Test",
                   QString("message with \"quotes\" and a backslash \\ and unicode \u00e9"));

        QString path = logger.currentLogFilePath();
        QVERIFY(QFile::exists(path));

        auto entries = StructuredLogger::readEntries(path);
        QCOMPARE(entries.size(), 4);

        QCOMPARE(entries[0].category, StructuredLogger::Category::Safety);
        QCOMPARE(entries[0].subsystem, QString("GantryAxisController"));
        QCOMPARE(entries[0].message, QString("Homing timed out — home switch not reached."));
        QVERIFY(entries[0].timestamp.isValid());

        QCOMPARE(entries[1].category, StructuredLogger::Category::Connection);
        QCOMPARE(entries[2].category, StructuredLogger::Category::Motion);

        QCOMPARE(entries[3].category, StructuredLogger::Category::Error);
        QCOMPARE(entries[3].message,
                 QString("message with \"quotes\" and a backslash \\ and unicode \u00e9"));
    }

    void testCategoryNameRoundTrip()
    {
        const QList<StructuredLogger::Category> all = {
            StructuredLogger::Category::Safety,
            StructuredLogger::Category::Motion,
            StructuredLogger::Category::Connection,
            StructuredLogger::Category::Error,
            StructuredLogger::Category::Info,
        };
        for (auto cat : all) {
            QString name = StructuredLogger::categoryName(cat);
            QCOMPARE(StructuredLogger::categoryFromName(name), cat);
        }
    }

    void testReadEntriesOnMissingFileReturnsEmpty()
    {
        auto entries = StructuredLogger::readEntries("Z:/definitely/does/not/exist.jsonl");
        QVERIFY(entries.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestStructuredLogger)
#include "test_structured_logger.moc"
