// ═══════════════════════════════════════════════════════════════════════════════
// Test: AxisManager — axis lookup, link sharing, and drive-kind switching.
//
// The interesting cases are all about identity: which controller is driving an
// axis right now, and whether two axes on one board share a transport. Getting
// either wrong produces the failure that has recurred throughout this work —
// commands going to the axis nobody is watching, with no error anywhere.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "infrastructure/gantry/axis_manager.h"
#include "infrastructure/gantry/axis_board_link.h"
#include "infrastructure/gantry/axis_controller_base.h"
#include "infrastructure/gantry/gantryaxiscontroller.h"
#include "infrastructure/gantry/stepper_axis_controller.h"

namespace {

AxisConfig makeAxis(const QString& id, AxisDriveKind kind,
                    const QString& port = "COM_FAKE", int index = 0)
{
    AxisConfig a;
    a.id                = id;
    a.displayName       = id;
    a.portName          = port;
    a.firmwareAxisIndex = index;
    a.motorSpec.driveKind = kind;
    a.tuning.countsPerUnit = 80.0;
    a.tuning.travelLimits  = {0.0, 500.0};
    return a;
}

} // namespace

class TestAxisManager : public QObject
{
    Q_OBJECT
private slots:

    // ─── Lookup ───────────────────────────────────────────────────────────

    void testConfiguredAxisIsFound()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry", AxisDriveKind::DcServoPwm) });

        QVERIFY(mgr.hasAxis("gantry"));
        QVERIFY(mgr.controller("gantry") != nullptr);
        QCOMPARE(mgr.axisIds(), QStringList{"gantry"});
    }

    void testUnknownAxisReturnsNullNotGarbage()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry", AxisDriveKind::DcServoPwm) });

        QVERIFY(!mgr.hasAxis("nope"));
        QCOMPARE(mgr.controller("nope"), nullptr);
    }

    /// Everything not yet migrated to per-axis lookups goes through primary(),
    /// so it must be the FIRST configured axis, not an arbitrary hash order.
    void testPrimaryIsTheFirstConfiguredAxis()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry",    AxisDriveKind::DcServoPwm,        "COM_FAKE", 0),
                        makeAxis("tilt_head", AxisDriveKind::StepDirClosedLoop, "COM_FAKE", 1) });

        QCOMPARE(mgr.primary(), mgr.controller("gantry"));
    }

    void testPrimaryIsNullWhenNothingConfigured()
    {
        AxisManager mgr(nullptr);
        QCOMPARE(mgr.primary(), nullptr);
    }

    // ─── Drive kind decides the concrete class ────────────────────────────

    void testDcAxisGetsTheServoController()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry", AxisDriveKind::DcServoPwm) });

        QVERIFY(qobject_cast<GantryAxisController*>(mgr.controller("gantry")) != nullptr);
    }

    void testStepperAxisGetsTheStepperController()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry", AxisDriveKind::StepDirClosedLoop) });

        QVERIFY(qobject_cast<StepperAxisController*>(mgr.controller("gantry")) != nullptr);
    }

    /// Changing the drive kind must swap the controller AND announce it.
    /// Consumers cache the pointer — the playback adapter above all — so a
    /// silent swap is how setpoints end up at the axis nobody is driving.
    void testChangingDriveKindSwapsControllerAndAnnounces()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry", AxisDriveKind::DcServoPwm) });
        AxisControllerBase* before = mgr.controller("gantry");

        QSignalSpy changed(&mgr, &AxisManager::activeControllerChanged);
        mgr.configure({ makeAxis("gantry", AxisDriveKind::StepDirClosedLoop) });
        AxisControllerBase* after = mgr.controller("gantry");

        QVERIFY(after != before);
        QVERIFY(qobject_cast<StepperAxisController*>(after) != nullptr);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().at(0).toString(), QString("gantry"));
    }

    /// Re-configuring with the SAME kind must not churn: no swap, no signal.
    /// configure() runs on every project load and settings change, so a
    /// needless swap would stop and restart a live axis each time.
    void testReconfiguringSameKindDoesNotSwap()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry", AxisDriveKind::DcServoPwm) });
        AxisControllerBase* before = mgr.controller("gantry");

        QSignalSpy changed(&mgr, &AxisManager::activeControllerChanged);
        mgr.configure({ makeAxis("gantry", AxisDriveKind::DcServoPwm) });

        QCOMPARE(mgr.controller("gantry"), before);
        QCOMPARE(changed.count(), 0);
    }

    // ─── Link sharing ─────────────────────────────────────────────────────

    /// Two axes on one port MUST share one link. Separate links would mean two
    /// QSerialPorts fighting over one device — the first to open wins and the
    /// second silently fails.
    void testAxesOnTheSamePortShareOneLink()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry",    AxisDriveKind::DcServoPwm,        "COM_A", 0),
                        makeAxis("tilt_head", AxisDriveKind::StepDirClosedLoop, "COM_A", 1) });

        QVERIFY(mgr.linkFor("gantry") != nullptr);
        QCOMPARE(mgr.linkFor("gantry"), mgr.linkFor("tilt_head"));
    }

    void testAxesOnDifferentPortsGetSeparateLinks()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry",    AxisDriveKind::DcServoPwm,        "COM_A", 0),
                        makeAxis("tilt_head", AxisDriveKind::StepDirClosedLoop, "COM_B", 0) });

        QVERIFY(mgr.linkFor("gantry") != mgr.linkFor("tilt_head"));
    }

    /// Axes whose port has not been chosen yet must still end up together —
    /// otherwise picking the port later leaves them on separate transports to
    /// what is physically one board.
    void testAxesWithNoPortYetStillShareALink()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("a", AxisDriveKind::DcServoPwm,        "", 0),
                        makeAxis("b", AxisDriveKind::StepDirClosedLoop, "", 1) });

        QCOMPARE(mgr.linkFor("a"), mgr.linkFor("b"));
    }

    // ─── Configuration reaches the controller ─────────────────────────────

    void testConfigureAppliesCalibrationAndLimits()
    {
        AxisManager mgr(nullptr);
        AxisConfig a = makeAxis("gantry", AxisDriveKind::DcServoPwm);
        a.tuning.countsPerUnit = 7.526;
        a.tuning.travelLimits  = {-15.5, 359.5};
        mgr.configure({ a });

        // Queued onto the axis thread; with no thread set it lands on ours.
        QTest::qWait(50);

        AxisControllerBase* c = mgr.controller("gantry");
        QCOMPARE(c->encoderCountsPerMm(), 7.526);
        QCOMPARE(c->travelLimits().minMm, -15.5);
        QCOMPARE(c->travelLimits().maxMm, 359.5);
    }

    /// Several axes on one board are distinguished only by their firmware
    /// index. Getting this wrong sends every axis's commands to axis 0.
    void testEachAxisKeepsItsOwnFirmwareIndex()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry",    AxisDriveKind::DcServoPwm,        "COM_A", 0),
                        makeAxis("tilt_head", AxisDriveKind::StepDirClosedLoop, "COM_A", 1) });

        QCOMPARE(mgr.controller("gantry")->axisIndex(), 0);
        QCOMPARE(mgr.controller("tilt_head")->axisIndex(), 1);
    }

    // ─── Concrete controllers stay reachable ──────────────────────────────

    /// Both concrete controllers must remain reachable whichever one is
    /// active. Consumers that are drive-kind SPECIFIC rather than "whatever
    /// is driving" — the PID tuning dialog, and the connect button before it
    /// was fixed — otherwise get nullptr and silently stop working.
    ///
    /// This is a real regression: deriving the DC pointer by casting the
    /// active controller nulled it whenever a stepper was selected, and
    /// switching drive kind stopped the board connecting at all.
    void testBothConcreteControllersSurviveASwitch()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry", AxisDriveKind::DcServoPwm) });

        GantryAxisController*  dc   = mgr.dcController("gantry");
        StepperAxisController* step = mgr.stepperController("gantry");
        QVERIFY(dc   != nullptr);
        QVERIFY(step != nullptr);
        QCOMPARE(mgr.controller("gantry"), static_cast<AxisControllerBase*>(dc));

        mgr.configure({ makeAxis("gantry", AxisDriveKind::StepDirClosedLoop) });

        // The ACTIVE one changed...
        QCOMPARE(mgr.controller("gantry"), static_cast<AxisControllerBase*>(step));
        // ...but both are still there, and are the SAME objects as before.
        QCOMPARE(mgr.dcController("gantry"), dc);
        QCOMPARE(mgr.stepperController("gantry"), step);
    }

    /// Connecting is a board-level action, so every controller on a shared
    /// link must reach the same one. That is what lets the connect button be
    /// routed through the active axis regardless of drive kind.
    void testBothControllersShareTheAxisLink()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry", AxisDriveKind::DcServoPwm) });

        AxisBoardLink* link = mgr.linkFor("gantry");
        QVERIFY(link != nullptr);

        mgr.configure({ makeAxis("gantry", AxisDriveKind::StepDirClosedLoop) });
        QCOMPARE(mgr.linkFor("gantry"), link);   // switching must not drop it
    }

    // ─── Board addressing ─────────────────────────────────────────────────

    /// The two drive kinds occupy DIFFERENT board addresses — the firmware
    /// enumerates "A 0 DC" and "A 1 STEP". Building both at one index made the
    /// stepper overwrite the DC controller as the handler for axis 0, so every
    /// Q and H reply went to the stepper: the DC axis stopped seeing its
    /// encoder and its limit switch, and homing timed out waiting for a reply
    /// that was being delivered elsewhere. The stepper meanwhile addressed
    /// axis 0 and the board rejected every command as "not a step axis".
    void testDriveKindsGetDifferentBoardAddresses()
    {
        AxisManager mgr(nullptr);
        AxisConfig a = makeAxis("gantry", AxisDriveKind::DcServoPwm);
        a.firmwareAxisIndex = 0;   // DC channel
        a.firmwareStepIndex = 1;   // STEP/DIR channel
        mgr.configure({ a });

        QCOMPARE(mgr.dcController("gantry")->axisIndex(), 0);
        QCOMPARE(mgr.stepperController("gantry")->axisIndex(), 1);
        QVERIFY2(mgr.dcController("gantry")->axisIndex()
                     != mgr.stepperController("gantry")->axisIndex(),
                 "the two kinds must never share a board address");
    }

    /// And the defaults must already be correct, because the first axis of
    /// every existing project is created from a default-constructed
    /// AxisConfig — that is exactly the path that broke.
    void testDefaultConfigAlreadySeparatesTheAddresses()
    {
        AxisManager mgr(nullptr);
        AxisConfig defaults;               // untouched
        mgr.configure({ defaults });

        QCOMPARE(mgr.dcController("gantry")->axisIndex(), 0);
        QCOMPARE(mgr.stepperController("gantry")->axisIndex(), 1);
    }
};

QTEST_MAIN(TestAxisManager)
#include "test_axis_manager.moc"
