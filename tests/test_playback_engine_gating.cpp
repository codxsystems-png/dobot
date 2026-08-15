// ═══════════════════════════════════════════════════════════════════════════════
// Test: PlaybackEngine robot-track gating — the second keyframe must NOT be
// sent to the adapter until the mock signals the first one's completion.
// This is the "fire-and-forget" bug: previously the engine fired every due
// keyframe purely on wall-clock time, with no idea whether the robot had
// actually finished the previous move.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "timeline/playback_engine.h"
#include "timeline/track_impls.h"
#include "hardware/mock_dobot_adapter.h"

using namespace timeline;

class TestPlaybackEngineGating : public QObject
{
    Q_OBJECT
private slots:
    void testSecondKeyframeWaitsForFirstCompletion()
    {
        auto tl = std::make_shared<Timeline>();
        auto robotTrack = std::make_shared<RobotTrack>("robot");

        // Slow move (low speed/accel %) so it stays in flight well past
        // kf1's due time — that's what proves the engine actually waited.
        hardware::DobotMoveTarget t0;
        t0.moveType = TimelineSegment::MovJ;
        t0.targetPose = CartesianPose{100.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        t0.speedPct = 10;
        t0.accPct = 10;

        hardware::DobotMoveTarget t1;
        t1.moveType = TimelineSegment::MovJ;
        t1.targetPose = CartesianPose{200.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        t1.speedPct = 100;
        t1.accPct = 100;

        TrackKeyframe kf0;
        kf0.id = "kf0";
        kf0.time = 0.0;
        kf0.value = QVariant::fromValue(t0);

        TrackKeyframe kf1;
        kf1.id = "kf1";
        kf1.time = 0.05; // due well before kf0's slow move can possibly finish
        kf1.value = QVariant::fromValue(t1);

        robotTrack->addKeyframe(kf0);
        robotTrack->addKeyframe(kf1);
        tl->addTrack(robotTrack);

        hardware::MockDobotAdapter adapter;

        PlaybackEngine engine;
        engine.addAdapter("robot", &adapter);
        engine.setTimeline(tl);

        QSignalSpy completedSpy(&adapter, &hardware::IDeviceAdapter::commandCompleted);

        engine.playFromStart();

        QVERIFY(QTest::qWaitFor([&]() { return adapter.isMoving(); }, 1000));

        // kf1's due time has now passed, but kf0's move is still in flight —
        // the second keyframe must NOT have been sent yet.
        QTest::qWait(200);
        QCOMPARE(completedSpy.count(), 0);
        QVERIFY(adapter.isMoving());
        QCOMPARE(adapter.currentPose().x, 0.0);

        // Let the first move actually finish.
        QVERIFY(completedSpy.wait(6000));
        QCOMPARE(adapter.currentPose().x, 100.0);

        // Only now should the engine retry and fire the held-back keyframe.
        QVERIFY(QTest::qWaitFor([&]() { return adapter.currentPose().x == 200.0; }, 6000));

        engine.stop();
    }

    void testStreamedModeSendsContinuousSetpointsInsteadOfDiscreteFires()
    {
        auto tl = std::make_shared<Timeline>();
        auto robotTrack = std::make_shared<RobotTrack>("robot");

        hardware::DobotMoveTarget t0;
        t0.moveType = TimelineSegment::MovJ;
        t0.targetPose = CartesianPose{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

        hardware::DobotMoveTarget t1;
        t1.moveType = TimelineSegment::MovJ;
        t1.targetPose = CartesianPose{100.0, 0.0, 0.0, 0.0, 0.0, 0.0};

        TrackKeyframe kf0;
        kf0.id = "kf0";
        kf0.time = 0.0;
        kf0.value = QVariant::fromValue(t0);

        TrackKeyframe kf1;
        kf1.id = "kf1";
        kf1.time = 1.0;
        kf1.value = QVariant::fromValue(t1);

        robotTrack->addKeyframe(kf0);
        robotTrack->addKeyframe(kf1);
        tl->addTrack(robotTrack);

        hardware::MockDobotAdapter adapter;

        PlaybackEngine engine;
        engine.setMode(PlaybackEngine::Mode::Streamed);
        engine.addAdapter("robot", &adapter);
        engine.setTimeline(tl);

        engine.playFromStart();

        // Mid-flight: streamed setpoints should already be arriving, and the
        // position should be somewhere strictly between the two keyframes —
        // not jumped straight to the end (that would mean it fired as a
        // discrete Fire-Together move instead of interpolating).
        QVERIFY(QTest::qWaitFor([&]() { return adapter.streamedSetpointCount() > 0; }, 1000));
        QTest::qWait(300);
        QVERIFY(adapter.streamedSetpointCount() > 1);
        QVERIFY(adapter.currentPose().x > 0.0);
        QVERIFY(adapter.currentPose().x < 100.0);

        // Fire-Together's discrete path must never have been used.
        QVERIFY(!adapter.isMoving());

        // Eventually settles at the final keyframe.
        QVERIFY(QTest::qWaitFor([&]() { return adapter.currentPose().x >= 99.9; }, 3000));

        engine.stop();
    }

    void testPlaybackAutoStopsWhenRobotDisconnected()
    {
        // Regression test: end-of-timeline detection used to treat "robot
        // not ready" as "robot still moving" without checking isConnected()
        // first. A disconnected adapter is also never "ready", so on a rig
        // with no Dobot attached (gantry-only, for example), playback could
        // never auto-stop — it just kept streaming setpoints to every other
        // connected device forever, leaving motors energized indefinitely.
        auto tl = std::make_shared<Timeline>();
        auto robotTrack = std::make_shared<RobotTrack>("robot");

        hardware::DobotMoveTarget t0;
        t0.moveType = TimelineSegment::MovJ;
        t0.targetPose = CartesianPose{50.0, 0.0, 0.0, 0.0, 0.0, 0.0};

        TrackKeyframe kf0;
        kf0.id = "kf0";
        kf0.time = 0.0;
        kf0.value = QVariant::fromValue(t0);
        robotTrack->addKeyframe(kf0);
        tl->addTrack(robotTrack);

        hardware::MockDobotAdapter adapter;
        adapter.setConnected(false); // simulate: no Dobot attached at all

        PlaybackEngine engine;
        engine.addAdapter("robot", &adapter);
        engine.setTimeline(tl);
        engine.setDuration(0.3); // short timeline

        QSignalSpy completedSpy(&engine, &PlaybackEngine::playbackCompleted);

        engine.playFromStart();

        QVERIFY2(completedSpy.wait(3000),
                 "playbackCompleted() never fired — playback did not auto-stop "
                 "when the robot adapter was disconnected");
        QCOMPARE(engine.currentState(), PlaybackEngine::State::Stopped);
    }
};

QTEST_MAIN(TestPlaybackEngineGating)
#include "test_playback_engine_gating.moc"
