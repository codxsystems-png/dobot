#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Playback Service (MRMC Engine Wrapper)
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include "core/types.h"
#include "timeline/playback_engine.h"

class ConnectionService;
class SegmentsModel;
class PointsModel;
class GantryService;
class FizService;
class AxisControllerBase;
class NucleusService;
class ProjectService;

namespace hardware {
    class DobotAdapter;
    class FizAdapter;
    class GantryAdapter;
}

class PlaybackService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(PlaybackState state READ state NOTIFY stateChanged)

public:
    enum PlaybackState {
        Stopped,
        Playing,
        Paused
    };
    Q_ENUM(PlaybackState)

    explicit PlaybackService(ConnectionService* connService,
                              SegmentsModel* segModel,
                              PointsModel* ptModel,
                              QObject* parent = nullptr);
    ~PlaybackService() override;

    PlaybackState state() const { return m_state; }
    double currentTime() const;
    int currentSegmentIndex() const { return -1; /* Deprecated */ }
    int totalSegments() const;

public slots:
    void play();
    void playFromStart();
    void pause();
    void stop();
    void emergencyStop();

    // Forwarded to the engine — previously m_looping was a dead member
    // nothing ever consulted, so toggling it had no effect on playback.
    void setLooping(bool loop) { m_engine->setLooping(loop); }
    bool isLooping() const { return m_engine->isLooping(); }

    void setModels(SegmentsModel* segModel, PointsModel* ptModel) {
        m_segModel = segModel;
        m_ptModel  = ptModel;
    }
    
    void setAdditionalServices(GantryService* gs, FizService* fs,
                               AxisControllerBase* axis, NucleusService* ns);

    // So compileAndLoadTimeline() can read the current gantry motor spec
    // (RPM/gear ratio/etc.) and pass it through to TimelineCompiler.
    void setProjectService(ProjectService* ps) { m_projectService = ps; }

    void setStartTime(double seconds);

signals:
    void stateChanged(PlaybackState newState);
    void playbackProgress(double currentTime, int segmentIndex);
    void playbackCompleted();
    void errorOccurred(const QString& error);
    void playheadTimeUpdated(double seconds);

private slots:
    void onEngineStateChanged(timeline::PlaybackEngine::State newState);

private:
    void compileAndLoadTimeline();

    ConnectionService* m_connService = nullptr;
    SegmentsModel*     m_segModel = nullptr;
    PointsModel*       m_ptModel = nullptr;
    GantryService*     m_gantryService = nullptr;
    FizService*        m_fizService = nullptr;
    ProjectService*    m_projectService = nullptr;

    timeline::PlaybackEngine* m_engine = nullptr;
    hardware::DobotAdapter*   m_dobotAdapter = nullptr;
    hardware::FizAdapter*     m_fizAdapter = nullptr;
    hardware::GantryAdapter*  m_gantryAdapter = nullptr;

    PlaybackState m_state = Stopped;
};

