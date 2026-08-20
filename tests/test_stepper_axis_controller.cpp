// ═══════════════════════════════════════════════════════════════════════════════
// Test: StepperAxisController — zeroing, target streaming, travel clamp, jog,
// alarm and watchdog handling, all through FakeSerialTransport (no hardware).
//
// The bias throughout is toward the failure paths. A stepper's position is
// what the host ASKED for, so the dangerous states are the ones where that
// assumption quietly stops holding: an alarm, a latched halt, a released axis,
// a reconnect. Those get more coverage here than the happy path does.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "infrastructure/gantry/stepper_axis_controller.h"
#include "infrastructure/gantry/axis_board_link.h"
#include "infrastructure/gantry/fake_serial_transport.h"
#include "hardware/gantry_adapter.h"

namespace {

const QString kVersionReply = "V FW=4 PROTO=3 BOARD=UNO AXES=3 CAPS=STEP";

/// Opens the port AND completes the version handshake. connectPort() alone
/// only opens it — motion is gated on the board identifying itself, because
/// opening the port asserts DTR and reboots an Uno.
void bringUp(StepperAxisController& axis, FakeSerialTransport* fake)
{
    axis.connectPort("COM_FAKE");
    fake->pushIncomingLine(kVersionReply);
    fake->emitReadyRead();
}

/// Brings the axis up and references it, which is the precondition for tick().
void bringUpAndZero(StepperAxisController& axis, FakeSerialTransport* fake)
{
    bringUp(axis, fake);
    axis.homeGantry();
    fake->clearWrittenCommands();
}

void reply(FakeSerialTransport* fake, const QString& line)
{
    fake->pushIncomingLine(line);
    fake->emitReadyRead();
}

} // namespace

class TestStepperAxisController : public QObject
{
    Q_OBJECT
private slots:

    // ─── Bring-up ─────────────────────────────────────────────────────────

    void testConnectSucceeds()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        QSignalSpy connectedSpy(&axis, &StepperAxisController::connected);

        QVERIFY(axis.connectPort("COM_FAKE"));
        QVERIFY(axis.isConnected());
        QCOMPARE(connectedSpy.count(), 1);
    }

    void testNotHomedUntilZeroed()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        bringUp(axis, fake);

        QVERIFY(!axis.isHomed());
    }

    /// The board keeps its step count in RAM, so a reconnect reboots it back
    /// to zero with no torque. Carrying the old origin across would let a
    /// reconnect command a move against a position that no longer exists.
    void testReconnectDiscardsOriginAndTorque()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        bringUpAndZero(axis, fake);
        QVERIFY(axis.isHomed());
        QVERIFY(axis.isEnabled());

        axis.connectPort("COM_FAKE");

        QVERIFY(!axis.isHomed());
        QVERIFY(!axis.isEnabled());
    }

    // ─── Set Zero Here ────────────────────────────────────────────────────

    void testHomeSetsZeroAndEnables()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        QSignalSpy homedSpy(&axis, &StepperAxisController::homed);

        bringUp(axis, fake);
        axis.homeGantry();

        QVERIFY(axis.isHomed());
        QCOMPARE(homedSpy.count(), 1);
        QCOMPARE(axis.currentPositionMm(), 0.0);
        QVERIFY(fake->wasCommandSent("Z 1"));
    }

    /// Zeroing a released axis would record an origin the shaft is free to
    /// drift away from before the first move ever runs.
    void testHomeAssertsTorqueBeforeZeroing()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        bringUp(axis, fake);
        fake->clearWrittenCommands();

        axis.homeGantry();

        const QStringList sent = fake->writtenCommands();
        QVERIFY(sent.contains("E 1 1"));
        QVERIFY(sent.contains("Z 1"));
        QVERIFY(sent.indexOf("E 1 1") < sent.indexOf("Z 1"));
    }

    // ─── Target streaming ─────────────────────────────────────────────────

    void testTickConvertsUnitsToSteps()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        axis.setEncoderCountsPerMm(80.0);          // 80 steps per unit
        axis.setTravelLimits({0.0, 500.0});
        bringUpAndZero(axis, fake);

        axis.tick(10.0);

        QVERIFY(fake->wasCommandSent("T 1 800"));
    }

    void testTickIgnoredUntilZeroed()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        axis.setTravelLimits({0.0, 500.0});
        bringUp(axis, fake);
        fake->clearWrittenCommands();

        axis.tick(10.0);

        QVERIFY(fake->commandsMatching("T ").isEmpty());
    }

    /// The board's watchdog watches the setpoint stream specifically, so an
    /// unchanged target still has to be re-sent — it is what tells the board
    /// the host is alive. Suppressing duplicates would make a parked axis look
    /// like a dead host.
    void testUnchangedTargetIsStillResentEveryTick()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        axis.setEncoderCountsPerMm(80.0);
        axis.setTravelLimits({0.0, 500.0});
        bringUpAndZero(axis, fake);

        axis.tick(10.0);
        axis.tick(10.0);
        axis.tick(10.0);

        QCOMPARE(fake->commandsMatching("T 1 800").count(), 3);
    }

    /// The mirror of the above: the heartbeat must NOT keep the setpoint
    /// alive. If it did, a hung playback thread would still drive the move to
    /// completion with nobody watching — which is exactly the failure the
    /// board's watchdog exists to catch.
    void testHeartbeatDoesNotRefreshTheTarget()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        axis.setEncoderCountsPerMm(80.0);
        axis.setTravelLimits({0.0, 500.0});
        bringUpAndZero(axis, fake);

        axis.tick(10.0);
        fake->clearWrittenCommands();

        for (int i = 0; i < 10; ++i) axis.heartbeat();

        QVERIFY(fake->commandsMatching("T ").isEmpty());
    }

    void testHeartbeatPollsPositionPeriodically()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        bringUpAndZero(axis, fake);

        for (int i = 0; i < 10; ++i) axis.heartbeat();

        // Polled at a fraction of the tick rate, not every tick.
        const int polls = fake->commandsMatching("Q 1").count();
        QCOMPARE(polls, 2);
    }

    // ─── Travel limits ────────────────────────────────────────────────────

    void testTargetIsClampedToTravel()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        axis.setEncoderCountsPerMm(80.0);
        axis.setTravelLimits({0.0, 100.0});
        bringUpAndZero(axis, fake);
        QSignalSpy errorSpy(&axis, &StepperAxisController::errorOccurred);

        axis.tick(250.0);   // well past the limit

        QVERIFY(fake->wasCommandSent("T 1 8000"));   // 100 units x 80
        QCOMPARE(errorSpy.count(), 1);
    }

    void testNegativeTargetIsClampedToo()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        axis.setEncoderCountsPerMm(80.0);
        axis.setTravelLimits({-50.0, 100.0});
        bringUpAndZero(axis, fake);

        axis.tick(-200.0);

        QVERIFY(fake->wasCommandSent("T 1 -4000"));
    }

    // ─── Position feedback ────────────────────────────────────────────────

    void testPositionReplyConvertsStepsToUnits()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        axis.setEncoderCountsPerMm(80.0);
        bringUpAndZero(axis, fake);
        QSignalSpy posSpy(&axis, &StepperAxisController::positionChanged);

        reply(fake, "Q 1 4000");

        QCOMPARE(axis.boardSteps(), 4000L);
        QCOMPARE(axis.currentPositionMm(), 50.0);
        QCOMPARE(posSpy.count(), 1);
    }

    /// A reply for another axis on the same board must not move this one.
    void testReplyForAnotherAxisIsIgnored()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        axis.setEncoderCountsPerMm(80.0);
        bringUpAndZero(axis, fake);

        reply(fake, "Q 0 999999");

        QCOMPARE(axis.boardSteps(), 0L);
        QCOMPARE(axis.currentPositionMm(), 0.0);
    }

    // ─── Jog ──────────────────────────────────────────────────────────────

    void testJogSendsRateAndIsResentByHeartbeat()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        bringUpAndZero(axis, fake);

        axis.jogGantry(2000);
        QVERIFY(fake->wasCommandSent("J 1 2000"));

        fake->clearWrittenCommands();
        axis.heartbeat();
        QVERIFY(fake->wasCommandSent("J 1 2000"));
    }

    void testJogIsClampedToVmax()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        bringUpAndZero(axis, fake);
        axis.setStepRateLimits(1000, 20000);

        axis.jogGantry(50000);

        QVERIFY(fake->wasCommandSent("J 1 1000"));
    }

    /// Stopping a jog leaves the board wherever it decelerated to, and it
    /// adopts that as its own target. The host has to follow, or the next
    /// tick would command a jump back to the pre-jog target.
    void testStopJogAdoptsTheBoardPosition()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        axis.setEncoderCountsPerMm(80.0);
        axis.setTravelLimits({0.0, 500.0});
        bringUpAndZero(axis, fake);

        axis.tick(10.0);                 // target 800
        axis.jogGantry(2000);
        reply(fake, "Q 1 3210");         // drifted well away while jogging
        axis.stopJog();

        QCOMPARE(axis.commandedSteps(), 3210L);
        QVERIFY(fake->wasCommandSent("J 1 0"));
    }

    void testTickIsIgnoredWhileJogging()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        axis.setEncoderCountsPerMm(80.0);
        axis.setTravelLimits({0.0, 500.0});
        bringUpAndZero(axis, fake);

        axis.jogGantry(2000);
        fake->clearWrittenCommands();
        axis.tick(10.0);

        QVERIFY(fake->commandsMatching("T ").isEmpty());
    }

    // ─── Alarm: the only real integrity signal this axis has ──────────────

    void testAlarmClearsHomedAndRaises()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        bringUpAndZero(axis, fake);
        QSignalSpy alarmSpy(&axis, &StepperAxisController::alarmRaised);
        QVERIFY(axis.isHomed());

        reply(fake, "! 1 1 drive alarm");

        QVERIFY(axis.isAlarmed());
        QVERIFY(!axis.isHomed());        // position is no longer trustworthy
        QCOMPARE(alarmSpy.count(), 1);
    }

    void testTickIsRefusedAfterAlarm()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        axis.setEncoderCountsPerMm(80.0);
        axis.setTravelLimits({0.0, 500.0});
        bringUpAndZero(axis, fake);

        reply(fake, "! 1 1 drive alarm");
        fake->clearWrittenCommands();
        axis.tick(10.0);

        QVERIFY(fake->commandsMatching("T ").isEmpty());
    }

    void testZeroingIsRefusedWhileAlarmed()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        bringUpAndZero(axis, fake);
        reply(fake, "! 1 1 drive alarm");

        QSignalSpy errorSpy(&axis, &StepperAxisController::errorOccurred);
        fake->clearWrittenCommands();
        axis.homeGantry();

        QVERIFY(!axis.isHomed());
        QVERIFY(fake->commandsMatching("Z ").isEmpty());
        QCOMPARE(errorSpy.count(), 1);
    }

    void testAlarmInStatusWordIsAlsoCaught()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        bringUpAndZero(axis, fake);

        reply(fake, "S 1 9 4000 0");     // 0x09 = enabled | alarm

        QVERIFY(axis.isAlarmed());
        QVERIFY(!axis.isHomed());
    }

    // ─── Watchdog and recovery ────────────────────────────────────────────

    void testWatchdogFaultLatchesHalted()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        axis.setEncoderCountsPerMm(80.0);
        axis.setTravelLimits({0.0, 500.0});
        bringUpAndZero(axis, fake);

        reply(fake, "! 1 2 setpoint stream stopped");

        QVERIFY(axis.isHalted());
        QVERIFY(!axis.isAlarmed());      // a stalled stream is not a drive fault
        QVERIFY(axis.isHomed());         // and it does not invalidate the origin

        fake->clearWrittenCommands();
        axis.tick(10.0);
        QVERIFY(fake->commandsMatching("T ").isEmpty());
    }

    /// The board adopts its own position as the target when it clears a fault,
    /// so a resumed link can never continue a move the operator has since lost
    /// track of. The host must land on the same number.
    void testClearFaultAdoptsTheBoardPosition()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        axis.setEncoderCountsPerMm(80.0);
        axis.setTravelLimits({0.0, 500.0});
        bringUpAndZero(axis, fake);

        axis.tick(30.0);                 // target 2400
        reply(fake, "Q 1 900");          // only got this far
        reply(fake, "! 1 2 setpoint stream stopped");

        axis.clearFault();

        QVERIFY(!axis.isHalted());
        QCOMPARE(axis.commandedSteps(), 900L);
        QVERIFY(fake->wasCommandSent("R 1"));
    }

    // ─── Release ──────────────────────────────────────────────────────────

    /// ENABLE is a torque switch. Releasing a loaded axis lets it fall, so the
    /// position reference cannot survive it.
    void testReleasingTorqueDiscardsTheOrigin()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        bringUpAndZero(axis, fake);
        QVERIFY(axis.isHomed());

        axis.setEnabled(false);

        QVERIFY(!axis.isEnabled());
        QVERIFY(!axis.isHomed());
        QVERIFY(fake->wasCommandSent("E 1 0"));
    }

    // ─── Stall detection ──────────────────────────────────────────────────

    void testStallIsReportedWhenTheBoardStopsFollowing()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        axis.setEncoderCountsPerMm(80.0);
        axis.setTravelLimits({0.0, 500.0});
        bringUpAndZero(axis, fake);
        QSignalSpy stallSpy(&axis, &StepperAxisController::driveStalled);

        axis.tick(30.0);                             // target 2400
        for (int i = 0; i < 12; ++i) reply(fake, "Q 1 100");   // pinned, never moves

        QCOMPARE(stallSpy.count(), 1);               // reported once, not per poll
        const auto args = stallSpy.first();
        QCOMPARE(args.at(0).toLongLong(), 2400LL);
        QCOMPARE(args.at(1).toLongLong(), 100LL);
    }

    void testProgressTowardTargetIsNotAStall()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        axis.setEncoderCountsPerMm(80.0);
        axis.setTravelLimits({0.0, 500.0});
        bringUpAndZero(axis, fake);
        QSignalSpy stallSpy(&axis, &StepperAxisController::driveStalled);

        axis.tick(30.0);
        for (int i = 1; i <= 12; ++i) reply(fake, QString("Q 1 %1").arg(i * 100));

        QCOMPARE(stallSpy.count(), 0);
    }

    /// A parked axis sits at its target indefinitely. That is not a stall, and
    /// reporting it as one would make every idle moment an error.
    void testSittingAtTargetIsNotAStall()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        axis.setEncoderCountsPerMm(80.0);
        axis.setTravelLimits({0.0, 500.0});
        bringUpAndZero(axis, fake);
        QSignalSpy stallSpy(&axis, &StepperAxisController::driveStalled);

        axis.tick(10.0);                             // target 800
        for (int i = 0; i < 30; ++i) reply(fake, "Q 1 800");

        QCOMPARE(stallSpy.count(), 0);
    }

    // ─── Configuration ────────────────────────────────────────────────────

    void testApplyTuningPushesCalibrationLimitsAndRates()
    {
        auto* fake = new FakeSerialTransport();
        StepperAxisController axis(fake);
        bringUp(axis, fake);
        axis.setStepRateLimits(6000, 30000);
        fake->clearWrittenCommands();

        GantryTuning t;
        t.countsPerUnit = 160.0;
        t.travelLimits  = {0.0, 250.0};
        axis.applyTuning(t);

        QCOMPARE(axis.encoderCountsPerMm(), 160.0);
        QCOMPARE(axis.travelLimits().maxMm, 250.0);
        QVERIFY(fake->wasCommandSent("L 1 6000 30000"));
    }

    // ─── Sharing one board link between two axes ──────────────────────────

    /// The UI opens the serial port once, on ONE controller. Every other axis
    /// on that link must still run its control loop — otherwise it looks
    /// connected, reports no position, and lets the board's watchdog stop it
    /// mid-move because nothing is refreshing the setpoint.
    ///
    /// This was a real bug: the control timer was created inside connectPort(),
    /// so an axis that never had connectPort() called on it never got one.
    void testSharedLinkStartsTheLoopForEveryAxis()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        StepperAxisController dc(&link, 0);
        StepperAxisController stepper(&link, 1);

        dc.connectPort("COM_FAKE");          // only the DC axis is connected
        fake->pushIncomingLine(kVersionReply);
        fake->emitReadyRead();

        fake->clearWrittenCommands();
        QTest::qWait(250);                   // several 20ms control ticks

        QVERIFY(stepper.isIdentified());
        QVERIFY(!fake->commandsMatching("Q 1").isEmpty());
    }

    /// The mirror: only one axis drives the timeline, and the other must go
    /// quiet — it must not spend link bandwidth or push position into a UI
    /// that is displaying the other axis.
    void testInactiveAxisStopsPolling()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        StepperAxisController dc(&link, 0);
        StepperAxisController stepper(&link, 1);

        stepper.setActive(false);
        dc.connectPort("COM_FAKE");
        fake->pushIncomingLine(kVersionReply);
        fake->emitReadyRead();

        fake->clearWrittenCommands();
        QTest::qWait(250);

        QVERIFY(fake->commandsMatching("Q 1").isEmpty());
        QVERIFY(!fake->commandsMatching("Q 0").isEmpty());   // the DC axis still runs
    }

    /// Re-identifying means the board rebooted, and its step count lives in
    /// RAM. Every axis on the link has to drop its origin, not just the one
    /// the port was opened on.
    void testIdentifyClearsOriginOnASharedLink()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        StepperAxisController dc(&link, 0);
        StepperAxisController stepper(&link, 1);

        dc.connectPort("COM_FAKE");
        fake->pushIncomingLine(kVersionReply);
        fake->emitReadyRead();

        stepper.homeGantry();
        QVERIFY(stepper.isHomed());

        // The board reboots and re-announces itself.
        fake->pushIncomingLine(kVersionReply);
        fake->emitReadyRead();

        QVERIFY(!stepper.isHomed());
    }

    // ─── Playback adapter ─────────────────────────────────────────────────

    /// The adapter is built once and stays registered with the playback engine
    /// under "gantry", so switching drive kind has to RE-POINT it. Miss this
    /// and the symptom is deeply misleading: the selected axis keeps reporting
    /// position perfectly while every move is streamed to the other one, so
    /// playback appears to do nothing at all.
    void testAdapterRepointsAtTheActiveAxis()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        StepperAxisController dc(&link, 0);
        StepperAxisController stepper(&link, 1);
        stepper.setEncoderCountsPerMm(80.0);
        stepper.setTravelLimits({0.0, 500.0});

        dc.connectPort("COM_FAKE");
        fake->pushIncomingLine(kVersionReply);
        fake->emitReadyRead();
        stepper.homeGantry();

        hardware::GantryAdapter adapter(&dc);
        QCOMPARE(adapter.controller(), static_cast<AxisControllerBase*>(&dc));

        adapter.setController(&stepper);
        QCOMPARE(adapter.controller(), static_cast<AxisControllerBase*>(&stepper));

        fake->clearWrittenCommands();
        adapter.sendStreamedSetpoint(QVariant(10.0));
        QTest::qWait(50);                     // the invoke is queued

        QVERIFY(fake->wasCommandSent("T 1 800"));
    }

    /// An axis created while the board is ALREADY identified must start its
    /// control loop immediately. The identified signal fires once per
    /// identification, and the operator adds axes from the setup dialog with
    /// the link live — so a late axis misses it and never sees it again.
    ///
    /// Real symptom: the new axis jogged for half a second and stopped (the
    /// board's watchdog, because nothing re-sent the jog) and its position
    /// readout stayed blank (because nothing polled it).
    void testAxisAddedWhileConnectedStartsItsLoop()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);

        StepperAxisController first(&link, 0);
        first.connectPort("COM_FAKE");
        fake->pushIncomingLine(kVersionReply);
        fake->emitReadyRead();
        QVERIFY(first.isIdentified());

        // Added AFTER identification, exactly as the setup dialog does.
        StepperAxisController late(&link, 1);
        QVERIFY(late.isIdentified());

        fake->clearWrittenCommands();
        QTest::qWait(250);

        QVERIFY2(!fake->commandsMatching("Q 1").isEmpty(),
                 "an axis added while connected must poll its own position");
    }

    /// The mirror: its jog must keep being re-sent, or the board's watchdog
    /// stops the axis after 500ms and it looks like "moves once, then stops".
    void testLateAddedAxisKeepsRefreshingItsJog()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);

        StepperAxisController first(&link, 0);
        first.connectPort("COM_FAKE");
        fake->pushIncomingLine(kVersionReply);
        fake->emitReadyRead();

        StepperAxisController late(&link, 1);
        late.homeGantry();
        late.jogGantry(500);

        fake->clearWrittenCommands();
        QTest::qWait(250);

        QVERIFY2(fake->commandsMatching("J 1 500").count() >= 2,
                 "the jog must be refreshed by the control loop, not sent once");
    }
};

QTEST_MAIN(TestStepperAxisController)
#include "test_stepper_axis_controller.moc"
