// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Playback Service
// ═══════════════════════════════════════════════════════════════════════════════

#include "services/playback_service.h"
#include "services/connection_service.h"
#include "models/segments_model.h"
#include "models/points_model.h"
#include "core/command_builder.h"
#include <QDebug>

static constexpr int TICK_INTERVAL_MS = 50; // 20Hz tick for timeline sync

#include "services/playback_service.h"
#include "services/connection_service.h"
#include "models/segments_model.h"
#include "models/points_model.h"
#include "application/gantryservice.h"
#include "application/fizservice.h"
#include "infrastructure/gantry/gantryaxiscontroller.h"
#include "infrastructure/fiz/nucleusservice.h"
#include "timeline/timeline_compiler.h"
#include "hardware/dobot_adapter.h"
#include "hardware/fiz_adapter.h"
#include "hardware/gantry_adapter.h"
#include "services/project_service.h"
#include <QDebug>

PlaybackService::PlaybackService(ConnectionService* connService,
                                   SegmentsModel* segModel,
                                   PointsModel* ptModel,
                                   QObject* parent)
    : QObject(parent)
    , m_connService(connService)
    , m_segModel(segModel)
    , m_ptModel(ptModel)
{
    m_engine = new timeline::PlaybackEngine(this);
    
    // Connect engine signals to our signals to maintain API compatibility
    connect(m_engine, &timeline::PlaybackEngine::playheadTimeUpdated, this, &PlaybackService::playheadTimeUpdated);
    connect(m_engine, &timeline::PlaybackEngine::playbackCompleted, this, &PlaybackService::playbackCompleted);
    connect(m_engine, &timeline::PlaybackEngine::errorOccurred, this, &PlaybackService::errorOccurred);
    connect(m_engine, &timeline::PlaybackEngine::stateChanged, this, &PlaybackService::onEngineStateChanged);
}

PlaybackService::~PlaybackService()
{
    stop();
}

void PlaybackService::setAdditionalServices(GantryService* gs, FizService* fs, 
                                            AxisControllerBase* axis, NucleusService* ns)
{
    m_gantryService = gs;
    m_fizService = fs;

    // Initialize adapters lazily
    if (!m_dobotAdapter) {
        m_dobotAdapter = new hardware::DobotAdapter(m_connService, this);
        m_engine->addAdapter("robot", m_dobotAdapter);
    }
    
    if (!m_fizAdapter && ns) {
        m_fizAdapter = new hardware::FizAdapter(ns, this);
        m_engine->addAdapter("fiz", m_fizAdapter);
    }
    
    if (axis) {
        if (!m_gantryAdapter) {
            m_gantryAdapter = new hardware::GantryAdapter(axis, this);
            m_engine->addAdapter("gantry", m_gantryAdapter);
        } else {
            // Re-called whenever the drive kind changes. The adapter is
            // created once and stays registered with the engine, so it has to
            // be re-pointed rather than rebuilt — the lazy-init guard alone
            // would leave it streaming to the previous axis forever.
            m_gantryAdapter->setController(axis);
        }
    }
}

void PlaybackService::registerAxisAdapter(const QString& axisId, AxisControllerBase* controller)
{
    if (axisId.isEmpty() || !controller || !m_engine) return;

    auto it = m_axisAdapters.find(axisId);
    if (it == m_axisAdapters.end()) {
        auto* adapter = new hardware::GantryAdapter(controller, this);
        m_axisAdapters.insert(axisId, adapter);
        m_engine->addAdapter(axisId, adapter);
    } else {
        // Re-point rather than rebuild: the adapter stays registered with the
        // engine under this id, so replacing it would leave the engine holding
        // the old one.
        (*it)->setController(controller);
    }
}

void PlaybackService::compileAndLoadTimeline()
{
    if (!m_engine) return;

    // Convert old UI models to MRMC Timeline
    const GantryMotorSpec* spec   = m_projectService ? &m_projectService->project().gantryMotorSpec : nullptr;
    const GantryTuning*    tuning = m_projectService ? &m_projectService->project().gantryTuning    : nullptr;

    // Every axis after the first gets its own track, with its own limits. The
    // primary stays on the GantryService path so existing projects compile to
    // exactly the timeline they always did.
    QList<timeline::AxisTrackInput> extraAxes;
    if (m_projectService) {
        const Project& p = m_projectService->project();
        for (const AxisConfig& axis : p.axes) {
            if (axis.id.isEmpty() || axis.id == "gantry") continue;
            timeline::AxisTrackInput in;
            in.config    = axis;
            in.keyframes = p.axisKeyframes.value(axis.id);
            extraAxes.append(in);
        }
    }

    auto timeline = timeline::TimelineCompiler::compile(m_segModel, m_ptModel, m_gantryService,
                                                        m_fizService, spec, tuning,
                                                        extraAxes.isEmpty() ? nullptr : &extraAxes);
    m_engine->setTimeline(timeline);
    // playbackCompleted() previously never fired because the engine had no
    // notion of "the end" — wire it to the same duration the timeline UI
    // already uses for scene sizing.
    m_engine->setDuration(m_segModel ? m_segModel->totalDuration() : -1.0);
}

double PlaybackService::currentTime() const
{
    return m_engine ? m_engine->currentTime() : 0.0;
}

int PlaybackService::totalSegments() const
{
    return m_segModel ? m_segModel->rowCount() : 0;
}

void PlaybackService::play()
{
    compileAndLoadTimeline();
    m_engine->play();
}

void PlaybackService::playFromStart()
{
    compileAndLoadTimeline();
    m_engine->playFromStart();
}

void PlaybackService::pause()
{
    m_engine->pause();
}

void PlaybackService::stop()
{
    m_engine->stop();
}

void PlaybackService::emergencyStop()
{
    m_engine->emergencyStop();
}

void PlaybackService::setStartTime(double seconds)
{
    // Not directly supported by engine API yet, but we can fast-forward if needed.
    // For now, it will start from wherever pause left it.
    Q_UNUSED(seconds)
}

void PlaybackService::onEngineStateChanged(timeline::PlaybackEngine::State newState)
{
    PlaybackState oldState = m_state;
    
    switch (newState) {
        case timeline::PlaybackEngine::State::Stopped: m_state = Stopped; break;
        case timeline::PlaybackEngine::State::Playing: m_state = Playing; break;
        case timeline::PlaybackEngine::State::Paused:  m_state = Paused; break;
    }
    
    if (m_state != oldState) {
        emit stateChanged(m_state);
    }
}
