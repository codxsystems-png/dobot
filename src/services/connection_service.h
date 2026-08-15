#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Connection Service
// Wires TCP layer to UI: connect/disconnect, startup sequence, E-STOP,
// jog commands, feedback relay.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QThread>
#include <QTimer>
#include "core/types.h"
#include "services/connection_state_machine.h"

// Forward declarations
class DobotTcpClient;
class RealtimeFeedbackWorker;
class CommandQueueManager;

class ConnectionService : public QObject
{
    Q_OBJECT
public:
    explicit ConnectionService(QThread* robotThread,
                                QThread* feedbackThread,
                                QObject* parent = nullptr);
    ~ConnectionService() override;

    bool isConnected() const { return m_connected; }

    /// Latest cached feedback (updated on every feedback packet, not throttled)
    JointAngles currentJoints() const { return m_lastJoints; }
    CartesianPose currentPose() const { return m_lastPose; }
    RobotMode currentMode() const { return m_lastMode; }

    ConnectionStateMachine::State connectionState() const { return m_stateMachine->state(); }

public slots:
    /// Connect to robot at given IP
    void connectToRobot(const QString& ip);

    /// Disconnect from robot
    void disconnectFromRobot();

    /// Emergency stop — bypasses all queues
    void emergencyStop();

    /// Clear error and re-enable after E-STOP
    void recoverFromEmergency();

    /// Jog a single axis (e.g. "J1+", "X-")
    void jogAxis(const QString& axis);

    /// Stop all jogging
    void jogStop();

    /// Toggle drag mode
    void setDragMode(bool enable);

    /// Set speed factor (1-100)
    void setSpeed(int speedPct);

    /// Enqueue a motion command to the robot
    int enqueueMotionCommand(const QString& command);

    /// Fire-and-forget send for high-frequency streamed servo commands
    /// (ServoJ/ServoP) — deliberately bypasses CommandQueueManager's
    /// ResultID tracking, which doesn't apply to a continuous setpoint
    /// stream. Currently goes out over the same dashboard socket as every
    /// other command here; real ~8ms-cadence streaming may need its own
    /// dedicated connection (see CommandBuilder::servoP's ASSUMPTION note).
    void sendStreamedCommand(const QString& command);

signals:
    /// Robot connection established (after full startup sequence)
    void connected();

    /// Robot disconnected
    void disconnected();

    /// Error occurred
    void errorOccurred(const QString& error);

    /// Connection state changed
    void connectionStateChanged(bool connected);

    /// Feedback data for StatusBarWidget (throttled to ~60Hz)
    void feedbackUpdated(const JointAngles& joints,
                         const CartesianPose& pose,
                         RobotMode mode,
                         int speedPct,
                         int queuePending);

    /// Relayed from CommandQueueManager — a queued command's ResultID has
    /// been confirmed complete (§8.5 ResultID tracking).
    void commandCompleted(int resultId);

private slots:
    void onDashboardConnected();
    void onFeedbackConnected();
    void onDashboardDisconnected();
    void onFeedbackDisconnected();
    void runStartupSequence();
    void onFeedbackReceived(const JointAngles& joints,
                            const CartesianPose& pose,
                            RobotMode mode,
                            uint64_t di, uint64_t dout);
    void onReconnectRequested();

private:
    DobotTcpClient*         m_dashClient     = nullptr;
    RealtimeFeedbackWorker* m_feedbackWorker  = nullptr;
    CommandQueueManager*    m_queueManager    = nullptr;
    ConnectionStateMachine* m_stateMachine    = nullptr;

    QThread*   m_robotThread    = nullptr;
    QThread*   m_feedbackThread = nullptr;
    QTimer*    m_feedbackThrottle = nullptr;

    QString    m_robotIp;
    bool       m_connected       = false;
    bool       m_dashConnected   = false;
    bool       m_feedConnected   = false;
    bool       m_intentionalDisconnect = false;
    int        m_speedPct        = 80;

    // Throttled feedback cache
    JointAngles   m_lastJoints;
    CartesianPose m_lastPose;
    RobotMode     m_lastMode = RobotMode::Init;
};
