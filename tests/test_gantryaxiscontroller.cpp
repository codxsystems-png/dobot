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

    // ─── Calibration / tuning wiring ──────────────────────────────────────────

    // Regression test for the root cause of "playback overshoots but jogging
    // is accurate": m_countsPerMm was hardcoded to 100.0 and the persisted
    // project value was never applied, so with a real ~1090 counts/mm encoder
    // the controller over-reported position ~11x, the PID chased a phantom
    // error into the end stop, and pinned at full PWM.
    void testCountsPerUnitConversionAppliesToReportedPosition()
    {
        auto* fake = new FakeSerialTransport();
        GantryAxisController gantry(fake);
        gantry.setEncoderCountsPerMm(1000.0);
        QCOMPARE(gantry.encoderCountsPerMm(), 1000.0);

        gantry.connectPort("COM_FAKE");
        gantry.homeGantry();
        gantry.heartbeat();
        fake->pushIncomingLine("1");
        fake->emitReadyRead();
        QVERIFY(gantry.isHomed());

        // Ask for the encoder, then have the Arduino report 5000 raw counts.
        gantry.heartbeat();
        fake->pushIncomingLine("5000");
        fake->emitReadyRead();

        // 5000 counts / 1000 counts-per-mm == 5mm. At the old hardcoded 100.0
        // this would have read 50mm — a 10x over-report.
        QCOMPARE(gantry.currentPositionMm(), 5.0);
    }

    void testApplyTuningPushesAllSettings()
    {
        auto* fake = new FakeSerialTransport();
        GantryAxisController gantry(fake);

        GantryTuning t;
        t.countsPerUnit = 250.0;
        t.travelLimits.minMm = -50.0;
        t.travelLimits.maxMm = 50.0;
        t.pwmRampPerTick = 40;
        t.pidKp = 1.5; t.pidKi = 0.2; t.pidKd = 0.01;
        t.configured = true;

        gantry.applyTuning(t);

        QCOMPARE(gantry.encoderCountsPerMm(), 250.0);
        QCOMPARE(gantry.travelLimits().minMm, -50.0);
        QCOMPARE(gantry.travelLimits().maxMm, 50.0);
        QCOMPARE(gantry.pwmRampPerTick(), 40);
    }

    void testCustomTravelLimitsClampInsteadOfDefault()
    {
        auto* fake = new FakeSerialTransport();
        GantryAxisController gantry(fake);
        GantryLimits limits; limits.minMm = -50.0; limits.maxMm = 50.0;
        gantry.setTravelLimits(limits);

        gantry.connectPort("COM_FAKE");
        gantry.homeGantry();
        gantry.heartbeat();
        fake->pushIncomingLine("1");
        fake->emitReadyRead();

        QSignalSpy errorSpy(&gantry, &GantryAxisController::errorOccurred);
        gantry.tick(500.0); // well past the configured 50mm, but under the 1000 default

        bool sawClampWarning = false;
        for (const auto& call : errorSpy) {
            if (call.at(0).toString().contains("clamped")) sawClampWarning = true;
        }
        QVERIFY2(sawClampWarning, "configured travel limits were ignored in favour of the default");
    }

    void testConfigurableRampLimitChangesFirstPwmStep()
    {
        auto* fake = new FakeSerialTransport();
        GantryAxisController gantry(fake);
        gantry.setPwmRampPerTick(40); // default is 15
        gantry.connectPort("COM_FAKE");
        gantry.homeGantry();
        gantry.heartbeat();
        fake->pushIncomingLine("1");
        fake->emitReadyRead();

        fake->clearWrittenCommands();
        gantry.tick(50.0);

        QVERIFY2(fake->wasPwmCommandSent(40), "ramp limit is not configurable");
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
