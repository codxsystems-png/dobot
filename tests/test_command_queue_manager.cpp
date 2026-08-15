// ═══════════════════════════════════════════════════════════════════════════════
// Test: CommandQueueManager — ResultID enqueue/poll/complete lifecycle,
// driven entirely through MockDobotTcpClient (no real socket needed).
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "network/command_queue_manager.h"
#include "network/mock_dobot_tcp_client.h"

class TestCommandQueueManager : public QObject
{
    Q_OBJECT
private slots:
    void testEnqueueReturnsScriptedResultId()
    {
        MockDobotTcpClient client;
        CommandQueueManager mgr(&client);

        client.queueResponse(/*errorId=*/0, /*resultId=*/100);
        int id = mgr.enqueueCommand("MovJ(...)");

        QCOMPARE(id, 100);
        QCOMPARE(mgr.pendingCount(), 1);
        QVERIFY(mgr.hasPendingCommands());
    }

    void testEnqueueFailsWhenDisconnected()
    {
        MockDobotTcpClient client;
        client.setConnected(false);
        CommandQueueManager mgr(&client);

        int id = mgr.enqueueCommand("MovJ(...)");

        QCOMPARE(id, -1);
        QCOMPARE(mgr.pendingCount(), 0);
    }

    void testEnqueueFailsOnNonZeroErrorId()
    {
        MockDobotTcpClient client;
        CommandQueueManager mgr(&client);

        client.queueResponse(/*errorId=*/5, /*resultId=*/999);
        int id = mgr.enqueueCommand("BadCommand()");

        QCOMPARE(id, -1);
        QCOMPARE(mgr.pendingCount(), 0);
    }

    void testPollingCompletesInOrderAsCurrentIdAdvances()
    {
        MockDobotTcpClient client;
        CommandQueueManager mgr(&client);

        client.queueResponse(0, 100);
        int id0 = mgr.enqueueCommand("Move 0");
        client.queueResponse(0, 101);
        int id1 = mgr.enqueueCommand("Move 1");
        QCOMPARE(id0, 100);
        QCOMPARE(id1, 101);
        QCOMPARE(mgr.pendingCount(), 2);

        QSignalSpy completedSpy(&mgr, &CommandQueueManager::commandCompleted);
        QSignalSpy allCompletedSpy(&mgr, &CommandQueueManager::allCommandsCompleted);

        // Robot has only finished the first command so far.
        client.setCurrentCommandId(100);
        mgr.startPolling(20);

        QVERIFY(completedSpy.wait(1000));
        QCOMPARE(completedSpy.count(), 1);
        QCOMPARE(completedSpy.at(0).at(0).toInt(), 100);
        QCOMPARE(mgr.pendingCount(), 1);
        QCOMPARE(mgr.lastCompletedId(), 100);
        QCOMPARE(allCompletedSpy.count(), 0); // one command (101) still pending

        // Robot now finishes the second command too.
        client.setCurrentCommandId(101);
        QVERIFY(completedSpy.wait(1000));
        QCOMPARE(completedSpy.count(), 2);
        QCOMPARE(completedSpy.at(1).at(0).toInt(), 101);
        QCOMPARE(mgr.pendingCount(), 0);
        QVERIFY(!mgr.hasPendingCommands());
        QCOMPARE(allCompletedSpy.count(), 1);

        mgr.stopPolling();
    }

    void testClearQueueDropsPendingCommands()
    {
        MockDobotTcpClient client;
        CommandQueueManager mgr(&client);

        client.queueResponse(0, 100);
        mgr.enqueueCommand("Move 0");
        QCOMPARE(mgr.pendingCount(), 1);

        mgr.clearQueue();
        QCOMPARE(mgr.pendingCount(), 0);
        QVERIFY(!mgr.hasPendingCommands());
    }
};

QTEST_MAIN(TestCommandQueueManager)
#include "test_command_queue_manager.moc"
