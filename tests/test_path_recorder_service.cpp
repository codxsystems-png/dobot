// ═══════════════════════════════════════════════════════════════════════════════
// Test: PathRecorderService — recording gate, and RDP path simplification.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "services/path_recorder_service.h"

class TestPathRecorderService : public QObject
{
    Q_OBJECT
private slots:
    void testAddSampleNoOpsWhenNotRecording()
    {
        PathRecorderService recorder;
        recorder.addSample(10.0);
        QCOMPARE(recorder.sampleCount(), 0);
    }

    void testRecordingAccumulatesSamples()
    {
        PathRecorderService recorder;
        recorder.startRecording();
        QVERIFY(recorder.isRecording());

        recorder.addSample(1.0);
        recorder.addSample(2.0);
        recorder.addSample(3.0);
        QCOMPARE(recorder.sampleCount(), 3);

        recorder.stopRecording();
        QVERIFY(!recorder.isRecording());

        // Stopped — no longer accepts samples.
        recorder.addSample(4.0);
        QCOMPARE(recorder.sampleCount(), 3);
    }

    void testStopRecordingEmitsSampleCount()
    {
        PathRecorderService recorder;
        QSignalSpy stoppedSpy(&recorder, &PathRecorderService::recordingStopped);

        recorder.startRecording();
        recorder.addSample(1.0);
        recorder.addSample(2.0);
        recorder.stopRecording();

        QCOMPARE(stoppedSpy.count(), 1);
        QCOMPARE(stoppedSpy.at(0).at(0).toInt(), 2);
    }

    void testSimplifyOnEmptyCaptureReturnsEmpty()
    {
        PathRecorderService recorder;
        QVERIFY(recorder.simplifyToGantryKeyframes().isEmpty());
    }

    void testStraightLineSimplifiesToEndpointsOnly()
    {
        // Explicit timestamps (not addSample()'s wall clock) so the shape
        // being tested doesn't depend on how fast this loop happens to run.
        PathRecorderService recorder;
        recorder.startRecording();
        // A perfectly straight line: every interior sample lies exactly on
        // the line between the first and last, so RDP should drop all of them.
        for (int i = 0; i <= 20; ++i) {
            recorder.addSampleAt(i * 0.1, i * 5.0); // t=0..2s, value 0..100
        }
        recorder.stopRecording();

        auto kfs = recorder.simplifyToGantryKeyframes(0.5);
        QCOMPARE(kfs.size(), 2);
        QCOMPARE(kfs.first().positionMm, 0.0);
        QCOMPARE(kfs.last().positionMm, 100.0);
    }

    void testVShapedPathKeepsTheElbow()
    {
        PathRecorderService recorder;
        recorder.startRecording();
        // Ramps up 0->100, then back down 100->0 — a clear elbow at the peak.
        double t = 0.0;
        for (int i = 0; i <= 10; ++i, t += 0.1) recorder.addSampleAt(t, i * 10.0);
        for (int i = 9; i >= 0; --i, t += 0.1) recorder.addSampleAt(t, i * 10.0);
        recorder.stopRecording();

        auto kfs = recorder.simplifyToGantryKeyframes(0.5);
        QCOMPARE(kfs.size(), 3); // start, peak, end
        QCOMPARE(kfs[1].positionMm, 100.0);
    }

    void testTighterToleranceKeepsMorePoints()
    {
        PathRecorderService recorder;
        recorder.startRecording();
        // A gentle S-curve-ish wiggle that a straight line only approximates.
        double values[] = {0, 5, 8, 20, 45, 60, 70, 72, 75, 100};
        double t = 0.0;
        for (double v : values) {
            recorder.addSampleAt(t, v);
            t += 0.1;
        }
        recorder.stopRecording();

        auto loose = recorder.simplifyToGantryKeyframes(50.0);
        auto tight = recorder.simplifyToGantryKeyframes(0.1);

        QVERIFY(tight.size() >= loose.size());
        QCOMPARE(loose.size(), 2); // large tolerance -> just the endpoints
    }

    void testSingleSampleProducesOneKeyframe()
    {
        PathRecorderService recorder;
        recorder.startRecording();
        recorder.addSampleAt(0.0, 42.0);
        recorder.stopRecording();

        auto kfs = recorder.simplifyToGantryKeyframes();
        QCOMPARE(kfs.size(), 1);
        QCOMPARE(kfs[0].positionMm, 42.0);
    }

    void testClearResetsCapture()
    {
        PathRecorderService recorder;
        recorder.startRecording();
        recorder.addSampleAt(0.0, 1.0);
        recorder.stopRecording();
        QCOMPARE(recorder.sampleCount(), 1);

        recorder.clear();
        QCOMPARE(recorder.sampleCount(), 0);
    }
};

QTEST_MAIN(TestPathRecorderService)
#include "test_path_recorder_service.moc"
