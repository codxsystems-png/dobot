// ═══════════════════════════════════════════════════════════════════════════════
// Test: GantryAxisController — homing, PID closed loop, travel-limit clamp,
// and fault handling, all driven through FakeSerialTransport (no hardware).
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "infrastructure/gantry/gantryaxiscontroller.h"
#include "infrastructure/gantry/fake_serial_transport.h"

class TestGantryAxisController : public QObject
{
    Q_OBJECT
private slots:
    void testConnectSucceeds()
    {
        auto* fake = new FakeSerialTransport();
        GantryAxisController gantry(fake);
        QSignalSpy connectedSpy(&gantry, &GantryAxisController::connected);

        QVERIFY(gantry.connectPort("COM_FAKE"));
        QVERIFY(gantry.isConnected());
        QCOMPARE(connectedSpy.count(), 1);
    }

    void testConnectFailurePropagates()
    {
        auto* fake = new FakeSerialTransport();
        fake->failNextOpen();
        GantryAxisController gantry(fake);
        QSignalSpy errorSpy(&gantry, &GantryAxisController::errorOccurred);

        QVERIFY(!gantry.connectPort("COM_FAKE"));
        QVERIFY(!gantry.isConnected());
        QVERIFY(errorSpy.count() >= 1);
    }

    void testHomingSequenceCompletesOnSwitchHit()
    {
        auto* fake = new FakeSerialTransport();
        GantryAxisController gantry(fake);
        QSignalSpy homedSpy(&gantry, &GantryAxisController::homed);

        QVERIFY(gantry.connectPort("COM_FAKE"));
        QVERIFY(!gantry.isHomed());

        gantry.homeGantry();
        gantry.heartbeat(); // drives toward the switch and sends an 'h' query
        QVERIFY(fake->wasPwmCommandSent(-100)); // default m_homePwm

        // Simulate the Arduino reporting the home switch has been reached.
        fake->pushIncomingLine("1");
        fake->emitReadyRead();

        QVERIFY(gantry.isHomed());
        QCOMPARE(homedSpy.count(), 1);
        QCOMPARE(gantry.currentPositionMm(), 0.0);
        QVERIFY(fake->wasPwmCommandSent(0)); // stops the motor on reaching home
    }

    void testClosedLoopDrivesTowardTarget()
    {
        auto* fake = new FakeSerialTransport();
        GantryAxisController gantry(fake);
        gantry.connectPort("COM_FAKE");
        gantry.homeGantry();
        gantry.heartbeat();
        fake->pushIncomingLine("1");
        fake->emitReadyRead();
        QVERIFY(gantry.isHomed());

        fake->clearWrittenCommands();
        gantry.tick(50.0); // target 50mm, currently at 0mm

        // First correction after a fresh reset uses dt=0 -> proportional-only,
        // then ramp-limited to MAX_PWM_CHANGE_PER_TICK (15) from a standing start.
        QVERIFY(fake->wasPwmCommandSent(15));
    }

    void testTravelLimitClampEmitsWarningAndClamps()
    {
        auto* fake = new FakeSerialTransport();
        GantryAxisController gantry(fake);
        gantry.connectPort("COM_FAKE");
        gantry.homeGantry();
        gantry.heartbeat();
        fake->pushIncomingLine("1");
        fake->emitReadyRead();

        QSignalSpy errorSpy(&gantry, &GantryAxisController::errorOccurred);
        gantry.tick(5000.0); // default GantryLimits max is 1000mm

        bool sawClampWarning = false;
        for (const auto& call : errorSpy) {
            if (call.at(0).toString().contains("clamped")) sawClampWarning = true;
        }
        QVERIFY(sawClampWarning);
    }

    void testFatalTransportErrorTearsDownAndSchedulesReconnect()
    {
        auto* fake = new FakeSerialTransport();
        GantryAxisController gantry(fake);
        gantry.connectPort("COM_FAKE");
        QVERIFY(gantry.isConnected());

        fake->emitFatalError("device disappeared");

        QVERIFY(!gantry.isConnected());
        QCOMPARE(gantry.connectionState(), ConnectionStateMachine::State::Reconnecting);
    }
};

QTEST_MAIN(TestGantryAxisController)
#include "test_gantryaxiscontroller.moc"
