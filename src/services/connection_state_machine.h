#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Connection State Machine (Phase 6: fault recovery)
// Shared reconnect/backoff policy applied uniformly to every transport
// (Dobot TCP dashboard, gantry serial, FIZ serial). This class owns no
// transport itself — the owning class (ConnectionService,
// GantryAxisController, ...) reports what actually happened via
// notifyConnecting()/notifyConnected()/notifyDisconnected()/notifyFault(),
// and this class decides when to ask for a reconnect attempt (with
// exponential backoff) via reconnectRequested().
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QString>
#include <QTimer>

class ConnectionStateMachine : public QObject
{
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Connected, Faulted, Reconnecting };
    Q_ENUM(State)

    explicit ConnectionStateMachine(QString subsystemName, QObject* parent = nullptr);

    State state() const { return m_state; }

    void setBackoffPolicy(int initialDelayMs, int maxDelayMs, double multiplier = 2.0);

public slots:
    /// A connect attempt has started.
    void notifyConnecting();

    /// The connect attempt succeeded. If this follows a fault (not a fresh
    /// connect), emits requiresReHome() once — the caller lost whatever
    /// homed/calibrated state it had while disconnected.
    void notifyConnected();

    /// A clean, user-initiated disconnect — goes straight to Disconnected,
    /// no auto-reconnect is scheduled.
    void notifyDisconnected(const QString& reason);

    /// An unexpected drop/error while connected (or while connecting) —
    /// goes to Faulted, then schedules an auto-reconnect attempt with
    /// exponential backoff.
    void notifyFault(const QString& reason);

    /// Cancels any pending reconnect attempt and forces Disconnected.
    void reset();

signals:
    void stateChanged(State newState);

    /// The backoff timer elapsed — the owner should attempt to reconnect
    /// now (e.g. call connectPort()/connectToRobot() again) and report the
    /// outcome via notifyConnected()/notifyFault().
    void reconnectRequested();

    /// Emitted once when recovering from a fault — the subsystem's prior
    /// homed/calibrated state can no longer be trusted.
    void requiresReHome();

private slots:
    void onBackoffTimeout();

private:
    void setState(State s);
    void scheduleReconnect();

    QString m_subsystemName;
    State   m_state = State::Disconnected;

    QTimer* m_backoffTimer = nullptr;
    int     m_initialDelayMs = 500;
    int     m_maxDelayMs     = 10000;
    double  m_multiplier     = 2.0;
    int     m_currentDelayMs = 500;

    bool m_recoveringFromFault = false;
};
