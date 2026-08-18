// ═══════════════════════════════════════════════════════════════════════════════
// Test: GantryAxisController — homing, PID closed loop, travel-limit clamp,
// and fault handling, all driven through FakeSerialTransport (no hardware).
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "infrastructure/gantry/gantryaxiscontroller.h"
#include "infrastructure/gantry/fake_serial_transport.h"

namespace {

const QString kVersionReply = "V FW=2 PROTO=2 BOARD=UNO AXES=1 CAPS=DC";

/// Opens the port AND completes the version handshake.
///
/// connectPort() alone only opens the port. Motion is gated on the board
/// having identified itself, because opening the port asserts DTR and reboots
/// an Uno — for ~1.6s there is no sketch running to talk to.
void bringUp(GantryAxisController& gantry, FakeSerialTransport* fake)
{
    gantry.connectPort("COM_FAKE");
    fake->pushIncomingLine(kVersionReply);
    fake->emitReadyRead();
}

} // namespace

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

        bringUp(gantry, fake);
        QVERIFY(!gantry.isHomed());

        gantry.homeGantry();
        gantry.heartbeat(); // drives toward the switch and sends an 'h' query
        QVERIFY(fake->wasPwmCommandSent(-100)); // default m_homePwm

        // Simulate the Arduino reporting the home switch has been reached.
        fake->pushIncomingLine("H 0 1");
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
        bringUp(gantry, fake);
        gantry.homeGantry();
        gantry.heartbeat();
        fake->pushIncomingLine("H 0 1");
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
        bringUp(gantry, fake);
        gantry.homeGantry();
        gantry.heartbeat();
        fake->pushIncomingLine("H 0 1");
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

        bringUp(gantry, fake);
        gantry.homeGantry();
        gantry.heartbeat();
        fake->pushIncomingLine("H 0 1");
        fake->emitReadyRead();
        QVERIFY(gantry.isHomed());

        // Ask for the encoder, then have the Arduino report 5000 raw counts.
        gantry.heartbeat();
        fake->pushIncomingLine("Q 0 5000");
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

        bringUp(gantry, fake);
        gantry.homeGantry();
        gantry.heartbeat();
        fake->pushIncomingLine("H 0 1");
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
        bringUp(gantry, fake);
        gantry.homeGantry();
        gantry.heartbeat();
        fake->pushIncomingLine("H 0 1");
        fake->emitReadyRead();

        fake->clearWrittenCommands();
        gantry.tick(50.0);

        QVERIFY2(fake->wasPwmCommandSent(40), "ramp limit is not configurable");
    }

    // ─── v2 protocol behaviour ────────────────────────────────────────────

    void testMotionRefusedUntilBoardIdentifiesItself()
    {
        // Opening the port reboots an Uno; for ~1.6s there is no sketch to
        // talk to. Commanding motion on "port opened" alone would mean every
        // connect drives into a bootloader.
        auto* fake = new FakeSerialTransport();
        GantryAxisController gantry(fake);

        QVERIFY(gantry.connectPort("COM_FAKE"));
        QVERIFY(gantry.isConnected());
        QVERIFY2(!gantry.isIdentified(), "must not be usable before the handshake");

        fake->clearWrittenCommands();
        gantry.homeGantry();
        QVERIFY2(!fake->wasPwmCommandSent(-100),
                 "homing must not drive the motor before the board identifies");

        // Board finally answers; now motion is allowed.
        fake->pushIncomingLine(kVersionReply);
        fake->emitReadyRead();
        QVERIFY(gantry.isIdentified());

        gantry.homeGantry();
        QVERIFY(fake->wasPwmCommandSent(-100));
    }

    void testUnsolicitedFaultDoesNotCorruptAPendingPositionRead()
    {
        // The v1 regression, directly: replies were matched positionally
        // against the outstanding query, so a fault arriving mid-poll was
        // consumed as the position and silently corrupted it.
        auto* fake = new FakeSerialTransport();
        GantryAxisController gantry(fake);
        gantry.setEncoderCountsPerMm(100.0);
        bringUp(gantry, fake);
        gantry.homeGantry();
        gantry.heartbeat();
        fake->pushIncomingLine("H 0 1");
        fake->emitReadyRead();

        gantry.heartbeat();
        fake->pushIncomingLine("! 0 3 limit");   // arrives before the answer
        fake->pushIncomingLine("Q 0 2500");
        fake->emitReadyRead();

        QCOMPARE(gantry.currentPositionMm(), 25.0);
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
