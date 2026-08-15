#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Command Queue Manager
// Tracks ResultIDs from queued motion commands.
// Polls GetCurrentCommandID() to detect move completion.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QTimer>
#include <QQueue>
#include "network/dobot_tcp_client.h"

struct QueuedCommand {
    int     resultId  = -1;
    QString command;
    bool    completed = false;
};

class CommandQueueManager : public QObject
{
    Q_OBJECT
public:
    explicit CommandQueueManager(DobotTcpClient* client, QObject* parent = nullptr);
    ~CommandQueueManager() override;

    /// Check if any commands are still pending.
    bool hasPendingCommands() const;

    /// Get the number of pending commands.
    int pendingCount() const;

    /// Get the last completed ResultID.
    int lastCompletedId() const;

    /// Get the current robot command ID (from last poll).
    int currentRobotId() const;

    bool isPolling() const;

public slots:
    // This object lives on robotThread (see ConnectionService), so every
    // entry point called from another thread MUST be a slot/Q_INVOKABLE —
    // QMetaObject::invokeMethod(obj, "name", ...) silently fails to resolve
    // (and does nothing) against a plain non-slot member function.

    /// Send a motion command, track its ResultID, and return the ID.
    /// Returns -1 on failure.
    int enqueueCommand(const QString& command);

    /// Clear the queue (e.g., after E-STOP).
    void clearQueue();

    /// Start/stop polling GetCurrentCommandID()
    void startPolling(int intervalMs = 100);
    void stopPolling();

signals:
    /// Emitted when a queued command is confirmed complete
    void commandCompleted(int resultId);

    /// Emitted when all queued commands are complete
    void allCommandsCompleted();

    /// Emitted each time GetCurrentCommandID is polled
    void currentIdUpdated(int currentId);

private slots:
    void pollCurrentId();

private:
    DobotTcpClient*      m_client     = nullptr;
    QTimer*              m_pollTimer   = nullptr;
    QQueue<QueuedCommand> m_queue;
    int                  m_currentRobotId = -1;
    int                  m_lastCompletedId = -1;
};
