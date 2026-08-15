#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Playback Engine
// Master clock and timeline evaluation.
// ═══════════════════════════════════════════════════════════════════════════════

#include "timeline/timeline.h"
#include "hardware/device_adapter.h"
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QList>

namespace timeline {

class PlaybackEngine : public QObject {
    Q_OBJECT
public:
    enum class State {
        Stopped,
        Playing,
        Paused
    };
    
    enum class Mode {
        FireTogether,
        Streamed
    };

    explicit PlaybackEngine(QObject* parent = nullptr);
    ~PlaybackEngine();

    void setTimeline(std::shared_ptr<Timeline> timeline) { m_timeline = timeline; }
    void addAdapter(const QString& trackId, hardware::IDeviceAdapter* adapter);

    void setMode(Mode mode) { m_mode = mode; }
    State currentState() const { return m_state; }
    double currentTime() const;

    /// Total timeline length, in seconds — playback stops and emits
    /// playbackCompleted() once currentTime() reaches this. <= 0 disables
    /// end-of-timeline detection (plays forever until stop()/pause()).
    void setDuration(double seconds) { m_duration = seconds; }

    void setLooping(bool loop) { m_looping = loop; }
    bool isLooping() const { return m_looping; }

public slots:
    void play();
    void playFromStart();
    void pause();
    void stop();
    void emergencyStop();

signals:
    void stateChanged(State newState);
    void playheadTimeUpdated(double timeSec);
    void playbackCompleted();
    void errorOccurred(const QString& msg);

private slots:
    void onTick();
    void onAdapterError(const QString& msg);

private:
    std::shared_ptr<Timeline> m_timeline;
    QMap<QString, hardware::IDeviceAdapter*> m_adapters;

    State m_state = State::Stopped;
    Mode m_mode = Mode::FireTogether;

    QElapsedTimer m_clock;
    QTimer* m_tickTimer = nullptr;
    
    double m_startTimeOffset = 0.0;
    double m_duration = -1.0;
    bool m_looping = false;

    // FireTogether state tracking
    double m_lastTickTime = 0.0;

    // Set when the most recently fired robot keyframe has pauseAfter=true;
    // once that move is confirmed complete (adapter->isReady() again),
    // onTick() pauses instead of firing the next keyframe.
    bool m_pendingPauseAfter = false;
};

} // namespace timeline
