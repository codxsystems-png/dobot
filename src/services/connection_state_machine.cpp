// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Connection State Machine
// ═══════════════════════════════════════════════════════════════════════════════

#include "services/connection_state_machine.h"
#include "core/structured_logger.h"
#include <QDebug>
#include <algorithm>

ConnectionStateMachine::ConnectionStateMachine(QString subsystemName, QObject* parent)
    : QObject(parent)
    , m_subsystemName(std::move(subsystemName))
{
    m_backoffTimer = new QTimer(this);
    m_backoffTimer->setSingleShot(true);
    connect(m_backoffTimer, &QTimer::timeout, this, &ConnectionStateMachine::onBackoffTimeout);
}

void ConnectionStateMachine::setBackoffPolicy(int initialDelayMs, int maxDelayMs, double multiplier)
{
    m_initialDelayMs = std::max(1, initialDelayMs);
    m_maxDelayMs     = std::max(m_initialDelayMs, maxDelayMs);
    m_multiplier     = std::max(1.0, multiplier);
    m_currentDelayMs = m_initialDelayMs;
}

void ConnectionStateMachine::notifyConnecting()
{
    m_backoffTimer->stop();
    setState(State::Connecting);
}

void ConnectionStateMachine::notifyConnected()
{
    m_backoffTimer->stop();
    m_currentDelayMs = m_initialDelayMs; // reset backoff — we're healthy again
    setState(State::Connected);

    if (m_recoveringFromFault) {
        m_recoveringFromFault = false;
        StructuredLogger::instance().log(StructuredLogger::Category::Safety,
            m_subsystemName, "Reconnected after fault — prior homed/calibrated state can't be trusted");
        emit requiresReHome();
    }
}

void ConnectionStateMachine::notifyDisconnected(const QString& reason)
{
    m_backoffTimer->stop();
    m_recoveringFromFault = false;
    StructuredLogger::instance().log(StructuredLogger::Category::Connection,
        m_subsystemName, "Disconnected: " + reason);
    setState(State::Disconnected);
}

void ConnectionStateMachine::notifyFault(const QString& reason)
{
    m_recoveringFromFault = true;
    StructuredLogger::instance().log(StructuredLogger::Category::Safety,
        m_subsystemName, "Fault: " + reason);
    setState(State::Faulted);
    scheduleReconnect();
}

void ConnectionStateMachine::reset()
{
    m_backoffTimer->stop();
    m_currentDelayMs = m_initialDelayMs;
    m_recoveringFromFault = false;
    setState(State::Disconnected);
}

void ConnectionStateMachine::scheduleReconnect()
{
    setState(State::Reconnecting);
    qDebug() << "ConnectionStateMachine[" << m_subsystemName << "]: reconnecting in"
             << m_currentDelayMs << "ms";
    m_backoffTimer->start(m_currentDelayMs);
    m_currentDelayMs = std::min(m_maxDelayMs, static_cast<int>(m_currentDelayMs * m_multiplier));
}

void ConnectionStateMachine::onBackoffTimeout()
{
    if (m_state != State::Reconnecting) return; // reset()/notifyConnected() ran in the meantime
    emit reconnectRequested();
}

void ConnectionStateMachine::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(m_state);
}
