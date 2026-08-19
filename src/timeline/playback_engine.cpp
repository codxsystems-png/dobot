#include "timeline/playback_engine.h"
#include "core/structured_logger.h"
#include "hardware/dobot_adapter.h"
#include <QDebug>

namespace timeline {

static constexpr int TICK_RATE_MS = 20; // 50Hz

PlaybackEngine::PlaybackEngine(QObject* parent)
    : QObject(parent)
{
    m_tickTimer = new QTimer(this);
    m_tickTimer->setTimerType(Qt::PreciseTimer);
    m_tickTimer->setInterval(TICK_RATE_MS);
    connect(m_tickTimer, &QTimer::timeout, this, &PlaybackEngine::onTick);
}

PlaybackEngine::~PlaybackEngine()
{
    stop();
}

void PlaybackEngine::addAdapter(const QString& trackId, hardware::IDeviceAdapter* adapter)
{
    if (adapter) {
        m_adapters[trackId] = adapter;
        connect(adapter, &hardware::IDeviceAdapter::errorOccurred, this, &PlaybackEngine::onAdapterError);
    }
}

double PlaybackEngine::currentTime() const
{
    if (m_state == State::Stopped) return m_startTimeOffset;
    if (m_state == State::Paused) return m_startTimeOffset;
    
    return m_startTimeOffset + m_clock.elapsed() / 1000.0;
}

void PlaybackEngine::play()
{
    if (m_state == State::Playing) return;

    if (!m_timeline) {
        emit errorOccurred("No timeline attached.");
        return;
    }

    QStringList preflightErrors;
    if (!m_timeline->validateLimits(preflightErrors)) {
        for (const QString& err : preflightErrors) {
            qWarning() << "Preflight error:" << err;
            StructuredLogger::instance().log(StructuredLogger::Category::Safety,
                "PlaybackEngine", "Preflight rejected: " + err);
        }
        // Surface the actual reasons — a bare "check failed" left operators
        // staring at a play button that silently does nothing.
        emit errorOccurred("Cannot start playback:\n\n" + preflightErrors.join("\n"));
        return;
    }

    // Devices that have keyframes on this timeline but aren't ready to move
    // (disconnected / not homed / not idle) would otherwise fail silently —
    // the gantry's tick() just no-ops when unhomed, so the playhead runs and
    // nothing physically moves. Warn loudly, but still allow playback (a
    // rehearsal with only some devices live is a legitimate use).
    QStringList notReady;
    for (const auto& track : m_timeline->allTracks()) {
        if (track->keyframes().isEmpty()) continue;
        auto it = m_adapters.constFind(track->trackId());
        if (it != m_adapters.constEnd() && !(*it)->isReady()) {
            notReady << (*it)->deviceName();
        }
    }
    if (!notReady.isEmpty()) {
        QString msg = QString("Not ready, will NOT move during this playback: %1 "
                              "(check connection / homing)").arg(notReady.join(", "));
        StructuredLogger::instance().log(StructuredLogger::Category::Safety,
            "PlaybackEngine", msg);
        emit errorOccurred(msg);
    }

    m_clock.restart();
    m_lastTickTime = m_startTimeOffset - 0.001;
    
    m_state = State::Playing;
    m_tickTimer->start();
    
    emit stateChanged(m_state);
    qDebug() << "PlaybackEngine: Play started at" << m_startTimeOffset << "s in mode" << (int)m_mode;
}

void PlaybackEngine::playFromStart()
{
    m_startTimeOffset = 0.0;
    play();
}

void PlaybackEngine::pause()
{
    if (m_state != State::Playing) return;

    m_startTimeOffset = currentTime();
    m_tickTimer->stop();
    m_state = State::Paused;

    for (auto* adapter : m_adapters) {
        adapter->stopMotion();
    }
    
    emit stateChanged(m_state);
    qDebug() << "PlaybackEngine: Paused at" << m_startTimeOffset << "s";
}

void PlaybackEngine::stop()
{
    if (m_state == State::Stopped) return;

    m_tickTimer->stop();
    m_startTimeOffset = 0.0;
    m_state = State::Stopped;
    m_pendingPauseAfter = false; // don't carry a stale pause flag into the next run

    for (auto* adapter : m_adapters) {
        adapter->stopMotion();
    }

    emit stateChanged(m_state);
    qDebug() << "PlaybackEngine: Stopped";
}

void PlaybackEngine::emergencyStop()
{
    m_tickTimer->stop();
    m_state = State::Stopped;
    
    for (auto* adapter : m_adapters) {
        adapter->emergencyStop();
    }

    emit stateChanged(m_state);
    emit errorOccurred("EMERGENCY STOP Triggered!");
    qWarning() << "PlaybackEngine: EMERGENCY STOP";
    StructuredLogger::instance().log(StructuredLogger::Category::Safety,
        "PlaybackEngine", "EMERGENCY STOP triggered");
}

namespace {
// Nothing here needs engine state; kept local so the resolution rule lives in
// exactly one place.
} // namespace

timeline::DeliveryMode PlaybackEngine::deliveryModeFor(const QString& trackId) const
{
    if (!m_timeline) return timeline::DeliveryMode::Streamed;
    for (const auto& track : m_timeline->allTracks()) {
        if (track->trackId() == trackId) return track->deliveryMode();
    }
    return timeline::DeliveryMode::Streamed;
}

bool PlaybackEngine::isStreamedNow(const QString& trackId) const
{
    const auto mode = deliveryModeFor(trackId);
    return mode == timeline::DeliveryMode::Streamed
        || (mode == timeline::DeliveryMode::StreamedOrWaypoint && m_mode == Mode::Streamed);
}

bool PlaybackEngine::isWaypointNow(const QString& trackId) const
{
    const auto mode = deliveryModeFor(trackId);
    return mode == timeline::DeliveryMode::Waypoint
        || (mode == timeline::DeliveryMode::StreamedOrWaypoint && m_mode == Mode::FireTogether);
}

hardware::IDeviceAdapter* PlaybackEngine::gatingAdapter() const
{
    for (auto it = m_adapters.constBegin(); it != m_adapters.constEnd(); ++it) {
        if (*it && (*it)->gatesPlaybackCompletion()) return *it;
    }
    return nullptr;
}

void PlaybackEngine::onTick()
{
    if (m_state != State::Playing || !m_timeline) return;

    double now = currentTime();
    emit playheadTimeUpdated(now);

    // 1. Evaluate streams for every track that delivers continuously. Which
    //    tracks those are is now the TRACK's answer (deliveryMode()), not a
    //    list of ids kept here — so a newly added axis streams without the
    //    engine being edited, and a misspelled id fails loudly at wiring time
    //    instead of being silently skipped every tick.
    QMap<QString, QVariant> frame = m_timeline->getFrame(now);
    for (auto it = frame.begin(); it != frame.end(); ++it) {
        auto adapterIt = m_adapters.constFind(it.key());
        if (adapterIt == m_adapters.constEnd()) continue;
        if (isStreamedNow(it.key())) {
            (*adapterIt)->sendStreamedSetpoint(it.value());
        }
    }

    // 2. Evaluate Waypoint Triggers for Robot (FireTogether mode only),
    //    gated on the PREVIOUS move actually completing — confirmed via
    //    ResultID/mode (see hardware::DobotAdapter::isReady()), not just
    //    wall-clock time. A due keyframe that can't fire yet stays due: the
    //    window doesn't advance past it, so it's retried next tick instead
    //    of firing blind (racing the robot's own queue) or being skipped.
    if (m_mode == Mode::FireTogether) {
        // Whichever device holds up completion is also the one a
        // pause-after-move waypoint waits on — both questions are "is there
        // still a discrete move in flight".
        hardware::IDeviceAdapter* robotAdapter = gatingAdapter();

        // A "pause after this move" waypoint fired last tick — hold here
        // (don't fire the next keyframe) once it's confirmed complete,
        // until the operator explicitly resumes. Gated on isConnected() too:
        // isReady()==false means either "still moving" OR "not connected at
        // all", and only the former should ever apply here (if the robot
        // isn't connected, m_pendingPauseAfter can't have been set anyway
        // since firing is itself gated on isReady()) — kept explicit so this
        // doesn't silently regress if that firing gate ever changes.
        if (m_pendingPauseAfter && robotAdapter && robotAdapter->isConnected() && robotAdapter->isReady()) {
            m_pendingPauseAfter = false;
            qDebug() << "PlaybackEngine: pausing after keyframe (Pause After Move)";
            StructuredLogger::instance().log(StructuredLogger::Category::Motion,
                "PlaybackEngine", "Auto-paused at pause-after-move waypoint");
            pause();
            return;
        }

        double robotWindowEnd = now;
        for (const auto& track : m_timeline->allTracks()) {
            QString tId = track->trackId();
            if (!m_adapters.contains(tId)) continue;

            if (isWaypointNow(tId)) {
                hardware::IDeviceAdapter* adapter = m_adapters[tId];
                auto kfs = track->keyframes();
                for (const auto& kf : kfs) {
                    // If the keyframe time falls strictly between last tick and now
                    if (kf.time > m_lastTickTime && kf.time <= now) {
                        if (!adapter->isReady()) {
                            robotWindowEnd = kf.time - 1e-6;
                            break;
                        }
                        qDebug() << "PlaybackEngine: Firing keyframe" << kf.id << "on track" << tId << "at time" << kf.time;
                        adapter->enqueueMoveCommand(kf.value, 0.0 /* duration not fully calced yet */);
                        if (kf.value.canConvert<hardware::DobotMoveTarget>()) {
                            m_pendingPauseAfter = kf.value.value<hardware::DobotMoveTarget>().pauseAfter;
                        }
                    }
                }
            }
        }
        m_lastTickTime = robotWindowEnd;
    }

    // 3. End-of-timeline detection. playbackCompleted() previously existed
    // as a signal (and was even relayed up through PlaybackService) but was
    // never actually emitted — playback just ran forever until the operator
    // hit Stop. Hold off completing while a robot move is still confirmed
    // in-flight so the last move isn't cut off right at the finish line.
    //
    // SAFETY FIX: this used to treat "robot not ready" as "still moving"
    // unconditionally — but isReady()==false also covers "never connected",
    // which meant playback could NEVER auto-stop whenever the Dobot simply
    // wasn't hooked up (a gantry-only rig, for example): the engine sat
    // there believing a robot move was perpetually in flight and kept
    // streaming setpoints to every OTHER connected device indefinitely.
    // Only a CONNECTED-but-not-ready robot should hold up completion.
    if (m_duration > 0.0 && now >= m_duration) {
        bool robotStillMoving = false;
        if (m_mode == Mode::FireTogether) {
            // Any gating device that is CONNECTED but not ready is genuinely
            // mid-move. Not-connected must never count, or playback on a rig
            // without that device could never end.
            for (auto it = m_adapters.constBegin(); it != m_adapters.constEnd(); ++it) {
                hardware::IDeviceAdapter* a = *it;
                if (!a || !a->gatesPlaybackCompletion()) continue;
                if (a->isConnected() && !a->isReady()) { robotStillMoving = true; break; }
            }
        }
        if (!robotStillMoving) {
            qDebug() << "PlaybackEngine: reached end of timeline at" << now << "s";
            if (m_looping) {
                playFromStart();
            } else {
                stop();
                emit playbackCompleted();
            }
        }
    }
}

void PlaybackEngine::onAdapterError(const QString& msg)
{
    qWarning() << "PlaybackEngine: Adapter Error -" << msg;
    StructuredLogger::instance().log(StructuredLogger::Category::Error,
        "PlaybackEngine", "Adapter error: " + msg);
    emergencyStop(); // Safety first
}

} // namespace timeline
