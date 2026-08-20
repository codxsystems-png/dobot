// ═══════════════════════════════════════════════════════════════════════════════
// Test: AxisManager — axis lookup, link sharing, and the limits it enforces.
//
// The interesting cases are all about identity: which controller serves an
// axis, and which axes share a transport or a board address. Getting either
// wrong produces the failure this area keeps producing — commands going to the
// axis nobody is watching, with no error anywhere.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "infrastructure/gantry/axis_manager.h"
#include "infrastructure/gantry/axis_board_link.h"
#include "infrastructure/gantry/stepper_axis_controller.h"

namespace {

AxisConfig makeAxis(const QString& id, const QString& port = "COM_FAKE", int index = 0)
{
    AxisConfig a;
    a.id                   = id;
    a.displayName          = id;
    a.portName             = port;
    a.firmwareAxisIndex    = index;
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
        mgr.configure({ makeAxis("gantry") });

        QVERIFY(mgr.hasAxis("gantry"));
        QVERIFY(mgr.controller("gantry") != nullptr);
        QCOMPARE(mgr.axisIds(), QStringList{"gantry"});
    }

    void testUnknownAxisReturnsNullNotGarbage()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry") });

        QVERIFY(!mgr.hasAxis("nope"));
        QCOMPARE(mgr.controller("nope"), nullptr);
    }

    /// Everything not yet migrated to per-axis lookups goes through primary(),
    /// so it must be the FIRST configured axis, not an arbitrary hash order.
    void testPrimaryIsTheFirstConfiguredAxis()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry", "COM_FAKE", 0),
                        makeAxis("tilt_head", "COM_FAKE", 1) });

        QCOMPARE(mgr.primary(), mgr.controller("gantry"));
    }

    void testPrimaryIsNullWhenNothingConfigured()
    {
        AxisManager mgr(nullptr);
        QCOMPARE(mgr.primary(), nullptr);
    }

    // ─── Link sharing ─────────────────────────────────────────────────────

    /// Axes on one port MUST share one link. Separate links would mean two
    /// QSerialPorts fighting over one device — the first to open wins and the
    /// second silently fails.
    void testAxesOnTheSamePortShareOneLink()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry", "COM_A", 0),
                        makeAxis("tilt_head", "COM_A", 1) });

        QVERIFY(mgr.linkFor("gantry") != nullptr);
        QCOMPARE(mgr.linkFor("gantry"), mgr.linkFor("tilt_head"));
    }

    void testAxesOnDifferentPortsGetSeparateLinks()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry", "COM_A", 0),
                        makeAxis("tilt_head", "COM_B", 0) });

        QVERIFY(mgr.linkFor("gantry") != mgr.linkFor("tilt_head"));
    }

    /// Axes whose port has not been chosen yet must still end up together —
    /// otherwise picking the port later leaves them on separate transports to
    /// what is physically one board.
    void testAxesWithNoPortYetStillShareALink()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("a", "", 0), makeAxis("b", "", 1) });

        QCOMPARE(mgr.linkFor("a"), mgr.linkFor("b"));
    }

    // ─── Board addressing ─────────────────────────────────────────────────

    /// Axes on one board are told apart ONLY by their address. Getting this
    /// wrong sends every axis's commands to the same channel.
    void testEachAxisKeepsItsOwnBoardAddress()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry", "COM_A", 0),
                        makeAxis("tilt_head", "COM_A", 1) });

        QCOMPARE(mgr.controller("gantry")->axisIndex(), 0);
        QCOMPARE(mgr.controller("tilt_head")->axisIndex(), 1);
    }

    /// A duplicate address must be REFUSED, not quietly accepted. The link
    /// routes replies by index, so the second axis would silently take over
    /// the first's position, faults and limit switch with no error anywhere —
    /// which is exactly how a previous version of this failed.
    void testDuplicateBoardAddressIsRefused()
    {
        AxisManager mgr(nullptr);
        QSignalSpy rejected(&mgr, &AxisManager::axisRejected);

        mgr.configure({ makeAxis("gantry", "COM_A", 0),
                        makeAxis("tilt_head", "COM_A", 0) });

        QVERIFY(mgr.hasAxis("gantry"));
        QVERIFY2(!mgr.hasAxis("tilt_head"),
                 "an axis duplicating a board address must not be created");
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(rejected.first().at(0).toString(), QString("tilt_head"));
    }

    /// The same address on a DIFFERENT board is fine — the clash is per port.
    void testSameAddressOnAnotherBoardIsAllowed()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry", "COM_A", 0),
                        makeAxis("tilt_head", "COM_B", 0) });

        QVERIFY(mgr.hasAxis("gantry"));
        QVERIFY(mgr.hasAxis("tilt_head"));
    }

    // ─── Axis count limit ─────────────────────────────────────────────────

    void testAxesUpToTheLimitAreAccepted()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("a", "COM_A", 0),
                        makeAxis("b", "COM_A", 1),
                        makeAxis("c", "COM_A", 2) });

        QCOMPARE(mgr.axisIds().size(), kMaxAxes);
    }

    /// Beyond the limit an axis is refused WITH a reason, rather than being
    /// half-created or silently dropped — the operator would otherwise have a
    /// configured axis that never moves and nothing explaining why.
    void testAxisBeyondTheLimitIsRefusedWithAReason()
    {
        AxisManager mgr(nullptr);
        QSignalSpy rejected(&mgr, &AxisManager::axisRejected);

        mgr.configure({ makeAxis("a", "COM_A", 0),
                        makeAxis("b", "COM_A", 1),
                        makeAxis("c", "COM_A", 2),
                        makeAxis("d", "COM_A", 3) });

        QCOMPARE(mgr.axisIds().size(), kMaxAxes);
        QVERIFY(!mgr.hasAxis("d"));
        QCOMPARE(rejected.count(), 1);
        QVERIFY2(!rejected.first().at(1).toString().isEmpty(),
                 "the rejection must carry a reason fit to show the operator");
    }

    // ─── Configuration reaches the controller ─────────────────────────────

    void testConfigureAppliesCalibrationAndLimits()
    {
        AxisManager mgr(nullptr);
        AxisConfig a = makeAxis("gantry");
        a.tuning.countsPerUnit = 7.526;
        a.tuning.travelLimits  = {-15.5, 359.5};
        mgr.configure({ a });

        // Queued onto the axis thread; with no thread set it lands on ours.
        QTest::qWait(50);

        StepperAxisController* c = mgr.controller("gantry");
        QCOMPARE(c->encoderCountsPerMm(), 7.526);
        QCOMPARE(c->travelLimits().minMm, -15.5);
        QCOMPARE(c->travelLimits().maxMm, 359.5);
    }

    /// configure() runs on every project load and settings change, so an axis
    /// that already exists must be reconfigured in place — rebuilding it would
    /// drop a live serial connection each time.
    void testReconfiguringKeepsTheSameController()
    {
        AxisManager mgr(nullptr);
        mgr.configure({ makeAxis("gantry") });
        StepperAxisController* before = mgr.controller("gantry");

        AxisConfig a = makeAxis("gantry");
        a.tuning.countsPerUnit = 400.0;
        mgr.configure({ a });
        QTest::qWait(50);

        QCOMPARE(mgr.controller("gantry"), before);
        QCOMPARE(before->encoderCountsPerMm(), 400.0);
    }
};

QTEST_MAIN(TestAxisManager)
#include "test_axis_manager.moc"
