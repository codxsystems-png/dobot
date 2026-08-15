// ═══════════════════════════════════════════════════════════════════════════════
// Test: ConnectionStateMachine — state transitions, backoff scheduling,
// re-home signal after fault recovery, and reset() cancelling a pending
// reconnect.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "services/connection_state_machine.h"

class TestConnectionStateMachine : public QObject
{
    Q_OBJECT
private slots:
    void testInitialStateIsDisconnected()
    {
        ConnectionStateMachine sm("Test");
        QCOMPARE(sm.state(), ConnectionStateMachine::State::Disconnected);
    }

    void testConnectingThenConnectedDoesNotRequireReHome()
    {
        ConnectionStateMachine sm("Test");
        QSignalSpy reHomeSpy(&sm, &ConnectionStateMachine::requiresReHome);

        sm.notifyConnecting();
        QCOMPARE(sm.state(), ConnectionStateMachine::State::Connecting);

        sm.notifyConnected();
        QCOMPARE(sm.state(), ConnectionStateMachine::State::Connected);
        QCOMPARE(reHomeSpy.count(), 0); // fresh connect, not a fault recovery
    }

    void testFaultSchedulesReconnectAndRecoveryRequiresReHome()
    {
        ConnectionStateMachine sm("Test");
        sm.setBackoffPolicy(20, 200, 2.0);

        sm.notifyConnecting();
        sm.notifyConnected();

        QSignalSpy reconnectSpy(&sm, &ConnectionStateMachine::reconnectRequested);
        QSignalSpy reHomeSpy(&sm, &ConnectionStateMachine::requiresReHome);

        sm.notifyFault("simulated drop");
        QCOMPARE(sm.state(), ConnectionStateMachine::State::Reconnecting);

        QVERIFY(reconnectSpy.wait(1000));
        QCOMPARE(reconnectSpy.count(), 1);

        // Owner attempts the reconnect and it succeeds — this is a recovery
        // from a fault, so requiresReHome() must fire.
        sm.notifyConnected();
        QCOMPARE(sm.state(), ConnectionStateMachine::State::Connected);
        QCOMPARE(reHomeSpy.count(), 1);
    }

    void testBackoffDelayGrowsBetweenConsecutiveFaults()
    {
        ConnectionStateMachine sm("Test");
        sm.setBackoffPolicy(30, 1000, 3.0);
        sm.notifyConnecting();
        sm.notifyConnected();

        QSignalSpy reconnectSpy(&sm, &ConnectionStateMachine::reconnectRequested);

        QElapsedTimer clock;

        clock.start();
        sm.notifyFault("fault 1");
        QVERIFY(reconnectSpy.wait(2000));
        qint64 firstDelayMs = clock.elapsed();

        // Owner's reconnect attempt fails immediately — fault again without
        // ever reaching Connected, so the backoff should have grown.
        clock.restart();
        sm.notifyFault("fault 2");
        QVERIFY(reconnectSpy.wait(2000));
        qint64 secondDelayMs = clock.elapsed();

        QVERIFY2(secondDelayMs > firstDelayMs * 1.5,
                 qPrintable(QString("expected backoff growth: first=%1ms second=%2ms")
                                .arg(firstDelayMs).arg(secondDelayMs)));
    }

    void testCleanDisconnectDoesNotScheduleReconnect()
    {
        ConnectionStateMachine sm("Test");
        sm.setBackoffPolicy(20, 200, 2.0);
        sm.notifyConnecting();
        sm.notifyConnected();

        QSignalSpy reconnectSpy(&sm, &ConnectionStateMachine::reconnectRequested);

        sm.notifyDisconnected("user requested");
        QCOMPARE(sm.state(), ConnectionStateMachine::State::Disconnected);

        QTest::qWait(300); // well past the backoff delay
        QCOMPARE(reconnectSpy.count(), 0);
    }

    void testResetCancelsPendingReconnect()
    {
        ConnectionStateMachine sm("Test");
        sm.setBackoffPolicy(20, 200, 2.0);
        sm.notifyConnecting();
        sm.notifyConnected();

        QSignalSpy reconnectSpy(&sm, &ConnectionStateMachine::reconnectRequested);

        sm.notifyFault("simulated drop");
        QCOMPARE(sm.state(), ConnectionStateMachine::State::Reconnecting);

        sm.reset();
        QCOMPARE(sm.state(), ConnectionStateMachine::State::Disconnected);

        QTest::qWait(300); // well past the backoff delay
        QCOMPARE(reconnectSpy.count(), 0);
    }
};

QTEST_MAIN(TestConnectionStateMachine)
#include "test_connection_state_machine.moc"
