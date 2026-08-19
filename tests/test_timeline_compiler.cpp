// ═══════════════════════════════════════════════════════════════════════════════
// Test: TimelineCompiler::compile() — SegmentsModel/PointsModel -> Timeline
// tracks (robot always, gantry/fiz only when those services are present).
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "timeline/timeline_compiler.h"
#include "timeline/track_impls.h"
#include "models/segments_model.h"
#include "models/points_model.h"
#include "application/gantryservice.h"
#include "application/fizservice.h"

using namespace timeline;

class TestTimelineCompiler : public QObject
{
    Q_OBJECT
private slots:
    void testEmptyModelsStillProduceRobotTrack()
    {
        PointsModel points;
        SegmentsModel segments(&points);

        auto tl = TimelineCompiler::compile(&segments, &points, nullptr, nullptr);

        QVERIFY(tl->track("robot") != nullptr);
        QCOMPARE(tl->track("robot")->keyframes().size(), 0);
        QVERIFY(tl->track("gantry") == nullptr);
        QVERIFY(tl->track("fiz") == nullptr);
    }

    void testMovJSegmentProducesMatchingRobotKeyframe()
    {
        PointsModel points;
        CameraPoint pt;
        pt.id = "pt1";
        pt.pose = CartesianPose{100.0, 50.0, 30.0, 10.0, 20.0, 30.0};
        points.addPoint(pt);

        SegmentsModel segments(&points);
        TimelineSegment seg;
        seg.id = "seg1";
        seg.pointId = "pt1";
        seg.type = TimelineSegment::MovJ;
        seg.triggerTime = 2.5;
        seg.speedPct = 70;
        seg.accPct = 40;
        seg.cpValue = 0.0;
        segments.addSegment(seg);

        auto tl = TimelineCompiler::compile(&segments, &points, nullptr, nullptr);

        auto robotTrack = tl->track("robot");
        QVERIFY(robotTrack != nullptr);
        auto kfs = robotTrack->keyframes();
        QCOMPARE(kfs.size(), 1);
        QCOMPARE(kfs[0].time, 2.5);

        auto target = kfs[0].value.value<hardware::DobotMoveTarget>();
        QCOMPARE(target.moveType, TimelineSegment::MovJ);
        QCOMPARE(target.targetPose.x, 100.0);
        QCOMPARE(target.targetPose.y, 50.0);
        QCOMPARE(target.speedPct, 70);
        QCOMPARE(target.accPct, 40);
    }

    void testGantryAndFizServicesAddCorrespondingTracks()
    {
        PointsModel points;
        CameraPoint pt;
        pt.id = "pt1";
        pt.pose = CartesianPose{0, 0, 0, 0, 0, 0};
        pt.gantryPositionMm = 42.0;
        pt.fizState = FizState{10.0f, 20.0f, 30.0f};
        points.addPoint(pt);

        SegmentsModel segments(&points);
        TimelineSegment seg;
        seg.id = "seg1";
        seg.pointId = "pt1";
        seg.type = TimelineSegment::MovJ;
        seg.triggerTime = 1.0;
        segments.addSegment(seg);

        GantryService gantry;
        FizService fiz;
        auto tl = TimelineCompiler::compile(&segments, &points, &gantry, &fiz);

        auto gantryTrack = tl->track("gantry");
        QVERIFY(gantryTrack != nullptr);
        QCOMPARE(gantryTrack->keyframes().size(), 1);
        QCOMPARE(gantryTrack->keyframes()[0].value.toDouble(), 42.0);

        auto fizTrack = tl->track("fiz");
        QVERIFY(fizTrack != nullptr);
        QCOMPARE(fizTrack->keyframes().size(), 1);
        FizState fizVal = fizTrack->keyframes()[0].value.value<FizState>();
        QCOMPARE(fizVal.focus, 10.0f);
        QCOMPARE(fizVal.iris, 20.0f);
        QCOMPARE(fizVal.zoom, 30.0f);
    }

    void testArcSegmentSetsViaPoseFromViaPoint()
    {
        PointsModel points;
        CameraPoint mainPt;
        mainPt.id = "main";
        mainPt.pose = CartesianPose{100, 0, 0, 0, 0, 0};
        points.addPoint(mainPt);

        CameraPoint viaPt;
        viaPt.id = "via";
        viaPt.pose = CartesianPose{50, 50, 0, 0, 0, 0};
        points.addPoint(viaPt);

        SegmentsModel segments(&points);
        TimelineSegment seg;
        seg.id = "seg1";
        seg.pointId = "main";
        seg.type = TimelineSegment::Arc;
        seg.arcViaPointId = "via";
        seg.triggerTime = 0.0;
        segments.addSegment(seg);

        auto tl = TimelineCompiler::compile(&segments, &points, nullptr, nullptr);
        auto kfs = tl->track("robot")->keyframes();
        QCOMPARE(kfs.size(), 1);

        auto target = kfs[0].value.value<hardware::DobotMoveTarget>();
        QCOMPARE(target.moveType, TimelineSegment::Arc);
        QCOMPARE(target.viaPose.x, 50.0);
        QCOMPARE(target.viaPose.y, 50.0);
    }

    void testArcFallsBackToMovJWhenViaPointMissing()
    {
        PointsModel points;
        CameraPoint mainPt;
        mainPt.id = "main";
        mainPt.pose = CartesianPose{100, 0, 0, 0, 0, 0};
        points.addPoint(mainPt);

        SegmentsModel segments(&points);
        TimelineSegment seg;
        seg.id = "seg1";
        seg.pointId = "main";
        seg.type = TimelineSegment::Arc;
        seg.arcViaPointId = "does-not-exist";
        seg.triggerTime = 0.0;
        segments.addSegment(seg);

        auto tl = TimelineCompiler::compile(&segments, &points, nullptr, nullptr);
        auto kfs = tl->track("robot")->keyframes();
        QCOMPARE(kfs.size(), 1);

        auto target = kfs[0].value.value<hardware::DobotMoveTarget>();
        QCOMPARE(target.moveType, TimelineSegment::MovJ); // fell back
    }

    void testSegmentWithMissingPointIsSkipped()
    {
        PointsModel points; // deliberately empty

        SegmentsModel segments(&points);
        TimelineSegment seg;
        seg.id = "seg1";
        seg.pointId = "does-not-exist";
        seg.type = TimelineSegment::MovJ;
        seg.triggerTime = 0.0;
        segments.addSegment(seg);

        auto tl = TimelineCompiler::compile(&segments, &points, nullptr, nullptr);
        QCOMPARE(tl->track("robot")->keyframes().size(), 0);
    }

    // ─── Gantry motor spec wiring (auto-computed segment timing feature) ───────

    std::shared_ptr<Timeline> compileTwoPointGantryMove(GantryService& gantry, double gapSec,
                                                          const GantryMotorSpec* spec)
    {
        PointsModel points;
        CameraPoint pt0;
        pt0.id = "pt0";
        pt0.gantryPositionMm = 0.0;
        points.addPoint(pt0);

        CameraPoint pt1;
        pt1.id = "pt1";
        pt1.gantryPositionMm = 500.0;
        points.addPoint(pt1);

        SegmentsModel segments(&points);
        TimelineSegment seg0;
        seg0.id = "seg0"; seg0.pointId = "pt0"; seg0.triggerTime = 0.0;
        segments.addSegment(seg0);
        TimelineSegment seg1;
        seg1.id = "seg1"; seg1.pointId = "pt1"; seg1.triggerTime = gapSec;
        segments.addSegment(seg1);

        return TimelineCompiler::compile(&segments, &points, &gantry, nullptr, spec);
    }

    void testGantryTrackUsesConfiguredMotorSpecLimits()
    {
        GantryService gantry;

        // A 500mm move in a 3s window is comfortably feasible at the
        // built-in default (200mm/s, 400mm/s^2).
        auto tlDefault = compileTwoPointGantryMove(gantry, 3.0, nullptr);
        QString err;
        QVERIFY(tlDefault->track("gantry")->validateLimits(err));

        // A very slow configured spec (4mm/s derived) makes the same move
        // infeasible in the same window — proving the spec is actually used.
        GantryMotorSpec slowSpec;
        slowSpec.motorRpm = 60.0;
        slowSpec.gearRatio = 10.0;
        slowSpec.mmPerRev = 4.0; // -> (60/10)*4/60 = 4 mm/s
        slowSpec.maxAccelMmPerSec2 = 10.0;
        slowSpec.configured = true;

        auto tlSlow = compileTwoPointGantryMove(gantry, 3.0, &slowSpec);
        QVERIFY(!tlSlow->track("gantry")->validateLimits(err));
        QVERIFY(err.contains("Gantry move"));
    }

    void testGantryTrackIgnoresUnconfiguredMotorSpec()
    {
        GantryService gantry;

        GantryMotorSpec unconfigured; // configured = false by default
        auto tlUnconfigured = compileTwoPointGantryMove(gantry, 3.0, &unconfigured);
        auto tlNullptr = compileTwoPointGantryMove(gantry, 3.0, nullptr);

        QString err1, err2;
        QCOMPARE(tlUnconfigured->track("gantry")->validateLimits(err1),
                 tlNullptr->track("gantry")->validateLimits(err2));
        QVERIFY(tlUnconfigured->track("gantry")->validateLimits(err1)); // both feasible at default limits
    }

    // ─── Additional external axes ─────────────────────────────────────────

    /// A second axis must compile to its own track under its own id. Without
    /// this the axis is configurable and connectable but has nothing on the
    /// timeline to drive it — it simply never moves.
    void testExtraAxisGetsItsOwnTrack()
    {
        PointsModel   pts;
        SegmentsModel segs(&pts);
        GantryService gantry;

        timeline::AxisTrackInput extra;
        extra.config.id = "tilt_head";
        GantryKeyframe k;
        k.id = "t1"; k.time = 1.0; k.positionMm = 45.0;
        extra.keyframes = { k };
        QList<timeline::AxisTrackInput> axes{ extra };

        auto tl = timeline::TimelineCompiler::compile(&segs, &pts, &gantry, nullptr,
                                                      nullptr, nullptr, &axes);

        bool found = false;
        for (const auto& t : tl->allTracks()) {
            if (t->trackId() == "tilt_head") {
                found = true;
                QCOMPARE(t->keyframes().size(), 1);
                QCOMPARE(t->keyframes().first().time, 1.0);
            }
        }
        QVERIFY2(found, "the extra axis must appear as its own track");
    }

    /// Each axis is limited by ITS OWN spec. Checking every axis against the
    /// primary's numbers would either reject reachable moves or, worse, admit
    /// moves a slower axis cannot make.
    void testExtraAxisUsesItsOwnTravelRange()
    {
        PointsModel   pts;
        SegmentsModel segs(&pts);
        GantryService gantry;

        timeline::AxisTrackInput extra;
        extra.config.id = "tilt_head";
        extra.config.tuning.configured   = true;
        extra.config.tuning.travelLimits = {0.0, 90.0};

        GantryKeyframe k;
        k.id = "t1"; k.time = 1.0; k.positionMm = 500.0;   // far outside 0..90
        extra.keyframes = { k };
        QList<timeline::AxisTrackInput> axes{ extra };

        auto tl = timeline::TimelineCompiler::compile(&segs, &pts, &gantry, nullptr,
                                                      nullptr, nullptr, &axes);

        QStringList errors;
        QVERIFY2(!tl->validateLimits(errors),
                 "a keyframe outside the extra axis's own travel range must be "
                 "rejected by preflight");
    }

    /// The primary axis is still compiled from GantryService, so passing extra
    /// axes must not disturb it — that is what keeps existing projects
    /// compiling to exactly the timeline they always did.
    void testExtraAxesDoNotDisturbThePrimary()
    {
        PointsModel   pts;
        SegmentsModel segs(&pts);
        GantryService gantry;
        GantryKeyframe g;
        g.id = "g1"; g.time = 2.0; g.positionMm = 10.0;
        gantry.addKeyframe(g);

        timeline::AxisTrackInput extra;
        extra.config.id = "tilt_head";
        QList<timeline::AxisTrackInput> axes{ extra };

        auto tl = timeline::TimelineCompiler::compile(&segs, &pts, &gantry, nullptr,
                                                      nullptr, nullptr, &axes);

        bool foundPrimary = false;
        for (const auto& t : tl->allTracks()) {
            if (t->trackId() == "gantry") {
                foundPrimary = true;
                QCOMPARE(t->keyframes().size(), 1);
            }
        }
        QVERIFY2(foundPrimary, "the primary gantry track must still be compiled");
    }

    /// An entry claiming to be the primary must be ignored rather than
    /// producing a second "gantry" track that silently shadows the real one.
    void testAxisClaimingThePrimaryIdIsIgnored()
    {
        PointsModel   pts;
        SegmentsModel segs(&pts);
        GantryService gantry;

        timeline::AxisTrackInput dupe;
        dupe.config.id = "gantry";
        QList<timeline::AxisTrackInput> axes{ dupe };

        auto tl = timeline::TimelineCompiler::compile(&segs, &pts, &gantry, nullptr,
                                                      nullptr, nullptr, &axes);

        int gantryTracks = 0;
        for (const auto& t : tl->allTracks()) {
            if (t->trackId() == "gantry") gantryTracks++;
        }
        QVERIFY2(gantryTracks <= 1, "must never compile two tracks with the same id");
    }
};

QTEST_MAIN(TestTimelineCompiler)
#include "test_timeline_compiler.moc"
