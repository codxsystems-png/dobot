#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Path Recorder Service (Phase 9: continuous-path teach)
// Records a continuous position stream while the operator jogs an axis by
// hand (an alternative to teaching only discrete fixed points), then
// resamples the raw capture into a sparse keyframe stream via Ramer-Douglas-
// Peucker simplification so a multi-second hand-jogged move doesn't become
// thousands of near-identical keyframes.
//
// Value-agnostic: feed it whatever continuous single-value position stream
// is available (today: StepperAxisController::positionChanged, in mm).
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QList>
#include <QElapsedTimer>
#include "core/types.h"

class PathRecorderService : public QObject
{
    Q_OBJECT
public:
    struct Sample {
        double time;  // seconds since recording started
        double value; // recorded position
    };

    explicit PathRecorderService(QObject* parent = nullptr);

    bool isRecording() const { return m_recording; }
    int sampleCount() const { return m_samples.size(); }
    QList<Sample> rawSamples() const { return m_samples; }

public slots:
    void startRecording();
    void stopRecording();

    /// Feed one live position sample, timestamped against the wall clock
    /// since startRecording(). No-ops when not currently recording, so this
    /// can stay connected to a live signal unconditionally.
    void addSample(double value);

    void clear();

    /// Add a sample with an explicit timestamp instead of the wall clock —
    /// for tests and for replaying a previously captured/loaded path. Still
    /// requires recording to be active.
    void addSampleAt(double time, double value);

signals:
    void recordingStarted();
    void recordingStopped(int sampleCount);

public:
    /// Resample the raw capture into GantryKeyframes, keeping only the
    /// points needed to reproduce the path within toleranceMm.
    QList<GantryKeyframe> simplifyToGantryKeyframes(double toleranceMm = 1.0) const;

private:
    bool m_recording = false;
    QElapsedTimer m_clock;
    QList<Sample> m_samples;
};
