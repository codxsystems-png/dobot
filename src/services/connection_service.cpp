// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Connection Service
// ═══════════════════════════════════════════════════════════════════════════════

#include "services/connection_service.h"
#include "network/dobot_tcp_client.h"
#include "network/realtime_feedback_worker.h"
#include "network/command_queue_manager.h"
#include "core/command_builder.h"
#include "core/structured_logger.h"
#include <QDebug>
#include <QThread>

ConnectionService::ConnectionService(QThread* robotThread,
                                       QThread* feedbackThread,
                                       QObject* parent)
    : QObject(parent)
    , m_robotThread(robotThread)
    , m_feedbackThread(feedbackThread)
{
    // Dashboard client lives on robotThread
    m_dashClient = new DobotTcpClient();
    m_dashClient->moveToThread(m_robotThread);

    // Feedback worker lives on feedbackThread
    m_feedbackWorker = new RealtimeFeedbackWorker();
    m_feedbackWorker->moveToThread(m_feedbackThread);

    // Queue manager lives on robotThread (polls via dashClient)
    m_queueManager = new CommandQueueManager(m_dashClient);
    m_queueManager->moveToThread(m_robotThread);

    // Relay ResultID completion to main thread (auto-queued: cross-thread signal)
    connect(m_queueManager, &CommandQueueManager::commandCompleted,
            this, &ConnectionService::commandCompleted);

    m_stateMachine = new ConnectionStateMachine("ConnectionService", this);
    m_stateMachine->setBackoffPolicy(1000, 15000, 2.0);
    connect(m_stateMachine, &ConnectionStateMachine::reconnectRequested,
            this, &ConnectionService::onReconnectRequested);
    connect(m_stateMachine, &ConnectionStateMachine::requiresReHome, this, [this]() {
        emit errorOccurred("Robot reconnected after a fault — re-verify mode/enable state before resuming motion.");
    });

    // Dashboard connection signals
    connect(m_dashClient, &DobotTcpClient::connected,
            this, &ConnectionService::onDashboardConnected, Qt::QueuedConnection);
    connect(m_dashClient, &DobotTcpClient::disconnected,
            this, &ConnectionService::onDashboardDisconnected, Qt::QueuedConnection);
    connect(m_dashClient, &DobotTcpClient::errorOccurred, this, [this](const QString& err) {
        StructuredLogger::instance().log(StructuredLogger::Category::Connection,
            "ConnectionService", err);
        emit errorOccurred(err);
    }, Qt::QueuedConnection);

    // Feedback connection signals
    connect(m_feedbackWorker, &RealtimeFeedbackWorker::connected,
            this, &ConnectionService::onFeedbackConnected, Qt::QueuedConnection);
    connect(m_feedbackWorker, &RealtimeFeedbackWorker::disconnected,
            this, &ConnectionService::onFeedbackDisconnected, Qt::QueuedConnection);

    // Feedback data relay (125Hz → throttled to 60Hz for UI)
    connect(m_feedbackWorker, &RealtimeFeedbackWorker::feedbackReceived,
            this, &ConnectionService::onFeedbackReceived, Qt::QueuedConnection);

    // 60Hz UI throttle timer
    m_feedbackThrottle = new QTimer(this);
    m_feedbackThrottle->setTimerType(Qt::PreciseTimer);
    m_feedbackThrottle->setInterval(16); // ~60Hz
    connect(m_feedbackThrottle, &QTimer::timeout, this, [this]() {
        int pending = m_queueManager ? m_queueManager->pendingCount() : 0;
        emit feedbackUpdated(m_lastJoints, m_lastPose, m_lastMode, m_speedPct, pending);
    });
}

ConnectionService::~ConnectionService()
{
    disconnectFromRobot();

    // Clean up workers (they live on other threads, deleteLater is safe)
    m_dashClient->deleteLater();
    m_feedbackWorker->deleteLater();
    m_queueManager->deleteLater();
}

// ─── Connect / Disconnect ───────────────────────────────────────────────────────

void ConnectionService::connectToRobot(const QString& ip)
{
    m_robotIp = ip;
    qDebug() << "ConnectionService: Connecting to" << ip;
    m_stateMachine->notifyConnecting();

    // Connect dashboard (port 29999) on robot thread
    QMetaObject::invokeMethod(m_dashClient, "connectToRobot",
                              Qt::QueuedConnection,
                              Q_ARG(QString, ip),
                              Q_ARG(quint16, 29999));

    // Connect feedback (port 30004) on feedback thread
    QMetaObject::invokeMethod(m_feedbackWorker, "connectToRobot",
                              Qt::QueuedConnection,
                              Q_ARG(QString, ip),
                              Q_ARG(quint16, 30004));
}

void ConnectionService::disconnectFromRobot()
{
    m_intentionalDisconnect = true;
    m_feedbackThrottle->stop();

    QMetaObject::invokeMethod(m_dashClient, "disconnectFromRobot", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_feedbackWorker, "disconnectFromRobot", Qt::QueuedConnection);

    m_connected     = false;
    m_dashConnected = false;
    m_feedConnected = false;
}

// ─── Startup Sequence (§8.1) ────────────────────────────────────────────────────

void ConnectionService::onDashboardConnected()
{
    qDebug() << "ConnectionService: Dashboard connected";
    m_dashConnected = true;

    if (m_feedConnected) {
        // Both ports connected — run startup sequence
        runStartupSequence();
    }
}

void ConnectionService::onFeedbackConnected()
{
    qDebug() << "ConnectionService: Feedback connected";
    m_feedConnected = true;
    m_feedbackThrottle->start();

    if (m_dashConnected) {
        runStartupSequence();
    }
}

void ConnectionService::runStartupSequence()
{
    qDebug() << "ConnectionService: Running startup sequence...";

    // §8.1: Set default speed cap on connect
    QMetaObject::invokeMethod(m_dashClient, [this]() {
        // 1. Check robot mode
        m_dashClient->sendCommand(CommandBuilder::robotMode());

        // 2. Set collision level
        m_dashClient->sendCommand(CommandBuilder::setCollisionLevel(3));

        // 3. Default speed cap (80%)
        m_dashClient->sendCommand(CommandBuilder::speedFactor(m_speedPct));
    }, Qt::QueuedConnection);

    // §8.5: Start ResultID polling so queued motion commands actually get a
    // commandCompleted signal — without this, PlaybackEngine's robot-move
    // gating (and anything else waiting on commandCompleted) never fires.
    if (m_queueManager) {
        QMetaObject::invokeMethod(m_queueManager, "startPolling", Qt::QueuedConnection,
                                  Q_ARG(int, 100));
    }

    m_connected = true;
    emit connected();
    emit connectionStateChanged(true);
    m_stateMachine->notifyConnected();
}

void ConnectionService::onDashboardDisconnected()
{
    m_dashConnected = false;
    m_connected = false;
    m_feedbackThrottle->stop();
    if (m_queueManager) {
        QMetaObject::invokeMethod(m_queueManager, "stopPolling", Qt::QueuedConnection);
    }

    if (m_intentionalDisconnect) {
        m_intentionalDisconnect = false;
        m_stateMachine->notifyDisconnected("user requested");
    } else {
        // Socket dropped without us asking for it — e.g. mid-playback.
        // §9.6 wants pending commands halted and a reconnection path, not
        // silence; the state machine schedules an auto-reconnect (with
        // backoff) and DobotAdapter forwards this as an adapter error so
        // PlaybackEngine E-STOPs instead of continuing to fire moves at a
        // dead connection.
        m_stateMachine->notifyFault("Dashboard connection lost unexpectedly");
    }

    emit disconnected();
    emit connectionStateChanged(false);
}

void ConnectionService::onFeedbackDisconnected()
{
    m_feedConnected = false;
    m_feedbackThrottle->stop();
}

void ConnectionService::onReconnectRequested()
{
    qDebug() << "ConnectionService: attempting reconnect to" << m_robotIp;
    connectToRobot(m_robotIp);
}

// ─── Feedback Relay ─────────────────────────────────────────────────────────────

void ConnectionService::onFeedbackReceived(const JointAngles& joints,
                                            const CartesianPose& pose,
                                            RobotMode mode,
                                            uint64_t di, uint64_t dout)
{
    Q_UNUSED(di)
    Q_UNUSED(dout)

    // Cache latest values — the throttle timer emits at 60Hz
    m_lastJoints = joints;
    m_lastPose   = pose;
    m_lastMode   = mode;
}

// ─── E-STOP ─────────────────────────────────────────────────────────────────────

void ConnectionService::emergencyStop()
{
    qWarning() << "ConnectionService: EMERGENCY STOP";
    StructuredLogger::instance().log(StructuredLogger::Category::Safety,
        "ConnectionService", "EMERGENCY STOP triggered");

    // §9.1: Bypass all queues — send immediately
    if (m_dashClient) {
        QMetaObject::invokeMethod(m_dashClient, [this]() {
            m_dashClient->sendCommandAsync(CommandBuilder::emergencyStop(1));
        }, Qt::QueuedConnection);
    }

    // Clear the command queue
    if (m_queueManager) {
        QMetaObject::invokeMethod(m_queueManager, "clearQueue", Qt::QueuedConnection);
        QMetaObject::invokeMethod(m_queueManager, "stopPolling", Qt::QueuedConnection);
    }
}

void ConnectionService::recoverFromEmergency()
{
    qDebug() << "ConnectionService: Recovering from E-STOP";
    StructuredLogger::instance().log(StructuredLogger::Category::Safety,
        "ConnectionService", "Recovering from E-STOP");

    QMetaObject::invokeMethod(m_dashClient, [this]() {
        // §9.1 Recovery: EmergencyStop(0) → ClearError() → EnableRobot()
        m_dashClient->sendCommand(CommandBuilder::emergencyStop(0));
        m_dashClient->sendCommand(CommandBuilder::clearError());
        m_dashClient->sendCommand(CommandBuilder::enableRobot());
    }, Qt::QueuedConnection);
}

// ─── Jog Controls ───────────────────────────────────────────────────────────────

void ConnectionService::jogAxis(const QString& axis)
{
    if (!m_connected) return;

    QMetaObject::invokeMethod(m_dashClient, [this, axis]() {
        m_dashClient->sendCommandAsync(CommandBuilder::moveJog(axis));
    }, Qt::QueuedConnection);
}

void ConnectionService::jogStop()
{
    if (!m_connected) return;

    // §8.3: MoveJog("") stops ALL jogging — ALWAYS send on release
    QMetaObject::invokeMethod(m_dashClient, [this]() {
        m_dashClient->sendCommandAsync(CommandBuilder::moveJog(""));
    }, Qt::QueuedConnection);
}

void ConnectionService::setDragMode(bool enable)
{
    if (!m_connected) return;

    QMetaObject::invokeMethod(m_dashClient, [this, enable]() {
        if (enable)
            m_dashClient->sendCommand(CommandBuilder::startDrag());
        else
            m_dashClient->sendCommand(CommandBuilder::stopDrag());
    }, Qt::QueuedConnection);
}

void ConnectionService::setSpeed(int speedPct)
{
    m_speedPct = qBound(1, speedPct, 100);

    if (!m_connected) return;

    QMetaObject::invokeMethod(m_dashClient, [this]() {
        m_dashClient->sendCommand(CommandBuilder::speedFactor(m_speedPct));
    }, Qt::QueuedConnection);
}

int ConnectionService::enqueueMotionCommand(const QString& command)
{
    if (!m_queueManager) return -1;
    int id = -1;
    QMetaObject::invokeMethod(m_queueManager, "enqueueCommand",
                              Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(int, id),
                              Q_ARG(QString, command));
    return id;
}

void ConnectionService::sendStreamedCommand(const QString& command)
{
    if (!m_connected) return;

    QMetaObject::invokeMethod(m_dashClient, [this, command]() {
        m_dashClient->sendCommandAsync(command);
    }, Qt::QueuedConnection);
}
