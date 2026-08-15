// ═══════════════════════════════════════════════════════════════════════════════
// Test: Track validateLimits() — real bounds/feasibility checks, not stubs.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "timeline/track_impls.h"

using namespace timeline;

class TestTrackLimits : public QObject
{
    Q_OBJECT
private slots:
    // ─── GantryTrack ────────────────────────────────────────────────────────

    void testGantryValidWithinDefaults()
    {
        GantryTrack track("gantry");
        track.addKeyframe(makeGantryKf("a", 0.0, 100.0));
        track.addKeyframe(makeGantryKf("b", 5.0, 300.0)); // 200mm over 5s, well under 200mm/s default

        QString err;
        QVERIFY(track.validateLimits(err));
        QVERIFY(err.isEmpty());
    }

    void testGantryRejectsOutOfRangePosition()
    {
        GantryTrack track("gantry");
        track.setRangeLimits(GantryLimits{0.0, 500.0});
        track.addKeyframe(makeGantryKf("a", 0.0, 100.0));
        track.addKeyframe(makeGantryKf("b", 5.0, 600.0)); // exceeds maxMm=500

        QString err;
        QVERIFY(!track.validateLimits(err));
        QVERIFY(err.contains("600"));
    }

    void testGantryRejectsInfeasibleMove()
    {
        GantryTrack track("gantry"); // default vMax=200mm/s, aMax=400mm/s^2
        track.addKeyframe(makeGantryKf("a", 0.0, 0.0));
        track.addKeyframe(makeGantryKf("b", 0.05, 1000.0)); // 1000mm in 50ms — impossible

        QString err;
        QVERIFY(!track.validateLimits(err));
        QVERIFY(err.contains("needs"));
    }

    // ─── FizTrack ───────────────────────────────────────────────────────────

    void testFizValidWithinDefaults()
    {
        FizTrack track("fiz");
        track.addKeyframe(makeFizKf("a", 0.0, {10.0f, 10.0f, 10.0f}));
        track.addKeyframe(makeFizKf("b", 5.0, {60.0f, 60.0f, 60.0f}));

        QString err;
        QVERIFY(track.validateLimits(err));
        QVERIFY(err.isEmpty());
    }

    void testFizRejectsOutOfRangeChannel()
    {
        FizTrack track("fiz");
        track.addKeyframe(makeFizKf("a", 0.0, {10.0f, 10.0f, 10.0f}));
        track.addKeyframe(makeFizKf("b", 5.0, {60.0f, 150.0f, 60.0f})); // iris > 100%

        QString err;
        QVERIFY(!track.validateLimits(err));
        QVERIFY(err.contains("iris"));
    }

    void testFizRejectsInfeasibleMove()
    {
        FizTrack track("fiz"); // default vMax=40%/s, aMax=150%/s^2
        track.addKeyframe(makeFizKf("a", 0.0, {0.0f, 0.0f, 0.0f}));
        track.addKeyframe(makeFizKf("b", 0.01, {100.0f, 0.0f, 0.0f})); // 100% in 10ms — impossible

        QString err;
        QVERIFY(!track.validateLimits(err));
        QVERIFY(err.contains("focus"));
    }

    // ─── RobotTrack ─────────────────────────────────────────────────────────

    void testRobotValidWithinDefaultWorkspace()
    {
        RobotTrack track("robot");
        track.addKeyframe(makeRobotKf("a", 0.0, CartesianPose{100.0, 100.0, 100.0, 0, 0, 0}));

        QString err;
        QVERIFY(track.validateLimits(err));
        QVERIFY(err.isEmpty());
    }

    void testRobotSampleAtInterpolatesBetweenKeyframes()
    {
        RobotTrack track("robot");
        track.addKeyframe(makeRobotKf("a", 0.0, CartesianPose{0.0, 0.0, 0.0, 0.0, 0.0, 0.0}));
        track.addKeyframe(makeRobotKf("b", 1.0, CartesianPose{100.0, 0.0, 0.0, 0.0, 0.0, 0.0}));

        QVariant midVal = track.sampleAt(0.5);
        QVERIFY(midVal.canConvert<hardware::DobotMoveTarget>());
        double midX = midVal.value<hardware::DobotMoveTarget>().targetPose.x;
        QVERIFY(midX > 0.0);
        QVERIFY(midX < 100.0);

        // Before the first / after the last keyframe, clamps to the endpoints.
        QCOMPARE(track.sampleAt(-1.0).value<hardware::DobotMoveTarget>().targetPose.x, 0.0);
        QCOMPARE(track.sampleAt(2.0).value<hardware::DobotMoveTarget>().targetPose.x, 100.0);
    }

    void testRobotSampleAtEmptyTrackReturnsInvalid()
    {
        RobotTrack track("robot");
        QVERIFY(!track.sampleAt(0.0).isValid());
    }

    void testRobotRejectsOutOfWorkspacePose()
    {
        RobotTrack track("robot");
        track.setWorkspaceLimits(WorkspaceLimits{-500.0, 500.0, -500.0, 500.0, 0.0, 500.0});
        track.addKeyframe(makeRobotKf("a", 0.0, CartesianPose{600.0, 0.0, 100.0, 0, 0, 0})); // x exceeds

        QString err;
        QVERIFY(!track.validateLimits(err));
        QVERIFY(err.contains("workspace envelope"));
    }

    void testRobotRejectsOutOfWorkspaceArcViaPoint()
    {
        RobotTrack track("robot");
        track.setWorkspaceLimits(WorkspaceLimits{-500.0, 500.0, -500.0, 500.0, 0.0, 500.0});

        hardware::DobotMoveTarget tgt;
        tgt.moveType = TimelineSegment::Arc;
        tgt.targetPose = CartesianPose{100.0, 0.0, 100.0, 0, 0, 0};
        tgt.viaPose    = CartesianPose{0.0, 0.0, 999.0, 0, 0, 0}; // z exceeds

        TrackKeyframe kf;
        kf.id = "a";
        kf.time = 0.0;
        kf.value = QVariant::fromValue(tgt);
        track.addKeyframe(kf);

        QString err;
        QVERIFY(!track.validateLimits(err));
        QVERIFY(err.contains("via-point"));
    }

    void testRobotRejectsInvalidPayload()
    {
        RobotTrack track("robot");
        TrackKeyframe kf;
        kf.id = "a";
        kf.time = 0.0;
        kf.value = QVariant::fromValue(QString("not a move target"));
        track.addKeyframe(kf);

        QString err;
        QVERIFY(!track.validateLimits(err));
    }

private:
    static TrackKeyframe makeGantryKf(const QString& id, double time, double posMm)
    {
        TrackKeyframe kf;
        kf.id = id;
        kf.time = time;
        kf.value = QVariant::fromValue(posMm);
        return kf;
    }

    static TrackKeyframe makeFizKf(const QString& id, double time, FizState state)
    {
        TrackKeyframe kf;
        kf.id = id;
        kf.time = time;
        kf.value = QVariant::fromValue(state);
        return kf;
    }

    static TrackKeyframe makeRobotKf(const QString& id, double time, const CartesianPose& pose)
    {
        hardware::DobotMoveTarget tgt;
        tgt.moveType = TimelineSegment::MovJ;
        tgt.targetPose = pose;

        TrackKeyframe kf;
        kf.id = id;
        kf.time = time;
        kf.value = QVariant::fromValue(tgt);
        return kf;
    }
};

QTEST_APPLESS_MAIN(TestTrackLimits)
#include "test_track_limits.moc"
