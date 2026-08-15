// ═══════════════════════════════════════════════════════════════════════════════
// Test: Mock device adapters — verify simulated Dobot/FIZ timing and error paths
// so the timeline/playback logic can be exercised without physical hardware.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "hardware/mock_dobot_adapter.h"
#include "hardware/mock_fiz_adapter.h"

using hardware::MockDobotAdapter;
using hardware::MockFizAdapter;

class TestMockAdapters : public QObject
{
    Q_OBJECT
private slots:
    void testDobotMoveCompletesAndUpdatesPose()
    {
        MockDobotAdapter adapter;
        QSignalSpy completedSpy(&adapter, &hardware::IDeviceAdapter::commandCompleted);

        hardware::DobotMoveTarget tgt;
        tgt.moveType = TimelineSegment::MovJ;
        tgt.targetPose = CartesianPose{100.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        tgt.speedPct = 80;
        tgt.accPct = 50;
        tgt.cpValue = 0.0;

        QVERIFY(adapter.isReady());
        adapter.enqueueMoveCommand(QVariant::fromValue(tgt), 0.0 /* let the mock estimate duration */);

        QVERIFY(adapter.isMoving());
        QVERIFY(!adapter.isReady());

        QVERIFY(completedSpy.wait(2000));
        QCOMPARE(completedSpy.count(), 1);

        QVERIFY(!adapter.isMoving());
        QVERIFY(adapter.isReady());
        QCOMPARE(adapter.currentPose().x, 100.0);
    }

    void testDobotMoveRespectsExplicitDuration()
    {
        MockDobotAdapter adapter;
        QSignalSpy completedSpy(&adapter, &hardware::IDeviceAdapter::commandCompleted);

        hardware::DobotMoveTarget tgt;
        tgt.moveType = TimelineSegment::MovL;
        tgt.targetPose = CartesianPose{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

        adapter.enqueueMoveCommand(QVariant::fromValue(tgt), 0.1);
        QVERIFY(adapter.isMoving());

        // Should not complete immediately.
        QVERIFY(!completedSpy.wait(20));
        // Should complete shortly after the requested duration.
        QVERIFY(completedSpy.wait(2000));
    }

    void testDobotRejectsInvalidPayload()
    {
        MockDobotAdapter adapter;
        QSignalSpy errorSpy(&adapter, &hardware::IDeviceAdapter::errorOccurred);

        adapter.enqueueMoveCommand(QVariant::fromValue(QString("not a move target")), 0.0);

        QCOMPARE(errorSpy.count(), 1);
        QVERIFY(!adapter.isMoving());
    }

    void testDobotRejectsWhenNotReady()
    {
        MockDobotAdapter adapter;
        adapter.setReady(false);
        QSignalSpy errorSpy(&adapter, &hardware::IDeviceAdapter::errorOccurred);

        hardware::DobotMoveTarget tgt;
        tgt.targetPose = CartesianPose{10.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        adapter.enqueueMoveCommand(QVariant::fromValue(tgt), 0.0);

        QCOMPARE(errorSpy.count(), 1);
        QVERIFY(!adapter.isMoving());
    }

    void testDobotStreamedSetpointNotImplemented()
    {
        MockDobotAdapter adapter;
        QSignalSpy errorSpy(&adapter, &hardware::IDeviceAdapter::errorOccurred);
        adapter.sendStreamedSetpoint(QVariant());
        QCOMPARE(errorSpy.count(), 1);
    }

    void testFizSetpointAppliesImmediately()
    {
        MockFizAdapter adapter;
        QSignalSpy completedSpy(&adapter, &hardware::IDeviceAdapter::commandCompleted);

        FizState state;
        state.focus = 25.0f;
        state.iris = 50.0f;
        state.zoom = 75.0f;

        adapter.sendStreamedSetpoint(QVariant::fromValue(state));

        QCOMPARE(completedSpy.count(), 1);
        QCOMPARE(adapter.lastState().focus, 25.0f);
        QCOMPARE(adapter.lastState().iris, 50.0f);
        QCOMPARE(adapter.lastState().zoom, 75.0f);
    }

    void testFizEnqueueMoveDelegatesToStreamedSetpoint()
    {
        MockFizAdapter adapter;
        QSignalSpy completedSpy(&adapter, &hardware::IDeviceAdapter::commandCompleted);

        FizState state;
        state.focus = 10.0f;
        adapter.enqueueMoveCommand(QVariant::fromValue(state), 0.0);

        QCOMPARE(completedSpy.count(), 1);
        QCOMPARE(adapter.lastState().focus, 10.0f);
    }

    void testFizRejectsInvalidPayload()
    {
        MockFizAdapter adapter;
        QSignalSpy errorSpy(&adapter, &hardware::IDeviceAdapter::errorOccurred);
        adapter.sendStreamedSetpoint(QVariant::fromValue(QString("nope")));
        QCOMPARE(errorSpy.count(), 1);
    }

    void testFizRejectsWhenNotReady()
    {
        MockFizAdapter adapter;
        adapter.setReady(false);
        QSignalSpy errorSpy(&adapter, &hardware::IDeviceAdapter::errorOccurred);
        adapter.sendStreamedSetpoint(QVariant::fromValue(FizState{}));
        QCOMPARE(errorSpy.count(), 1);
    }
};

QTEST_MAIN(TestMockAdapters)
#include "test_mock_adapters.moc"
