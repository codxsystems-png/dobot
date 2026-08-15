// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Command Queue Manager
// ═══════════════════════════════════════════════════════════════════════════════

#include "network/command_queue_manager.h"
#include "core/command_builder.h"
#include <QDebug>

CommandQueueManager::CommandQueueManager(DobotTcpClient* client, QObject* parent)
    : QObject(parent)
    , m_client(client)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setTimerType(Qt::PreciseTimer);
    connect(m_pollTimer, &QTimer::timeout,
            this, &CommandQueueManager::pollCurrentId);
}

CommandQueueManager::~CommandQueueManager()
{
    stopPolling();
}

int CommandQueueManager::enqueueCommand(const QString& command)
{
    if (!m_client || !m_client->isConnected()) {
        qWarning() << "CommandQueueManager: Client not connected";
        return -1;
    }

    ParsedResponse resp = m_client->sendCommand(command);

    if (!resp.valid || resp.errorId != 0) {
        qWarning() << "CommandQueueManager: Command failed:" << command.trimmed()
                    << "ErrorID:" << resp.errorId;
        return -1;
    }

    // The ResultID is typically the first value in the response
    int resultId = ResponseParser::extractInt(resp, 0);

    QueuedCommand qc;
    qc.resultId  = resultId;
    qc.command   = command.trimmed();
    qc.completed = false;
    m_queue.enqueue(qc);

    qDebug() << "CommandQueueManager: Enqueued" << qc.command
             << "ResultID:" << resultId
             << "Pending:" << pendingCount();

    return resultId;
}

bool CommandQueueManager::hasPendingCommands() const
{
    return !m_queue.isEmpty();
}

int CommandQueueManager::pendingCount() const
{
    return m_queue.size();
}

int CommandQueueManager::lastCompletedId() const
{
    return m_lastCompletedId;
}

int CommandQueueManager::currentRobotId() const
{
    return m_currentRobotId;
}

void CommandQueueManager::clearQueue()
{
    m_queue.clear();
    qDebug() << "CommandQueueManager: Queue cleared";
}

void CommandQueueManager::startPolling(int intervalMs)
{
    if (!m_pollTimer->isActive()) {
        m_pollTimer->start(intervalMs);
        qDebug() << "CommandQueueManager: Polling started every" << intervalMs << "ms";
    }
}

void CommandQueueManager::stopPolling()
{
    m_pollTimer->stop();
}

bool CommandQueueManager::isPolling() const
{
    return m_pollTimer->isActive();
}

void CommandQueueManager::pollCurrentId()
{
    if (!m_client || !m_client->isConnected())
        return;

    ParsedResponse resp = m_client->sendCommand(CommandBuilder::getCurrentCommandID(), 2000);
    if (!resp.valid)
        return;

    int newId = ResponseParser::extractInt(resp, 0);
    if (newId < 0)
        return;

    m_currentRobotId = newId;
    emit currentIdUpdated(m_currentRobotId);

    // Check if any queued commands have completed
    while (!m_queue.isEmpty()) {
        QueuedCommand& front = m_queue.head();
        if (m_currentRobotId >= front.resultId) {
            front.completed = true;
            m_lastCompletedId = front.resultId;

            qDebug() << "CommandQueueManager: Completed" << front.command
                     << "ResultID:" << front.resultId;

            int completedId = front.resultId;
            m_queue.dequeue();
            emit commandCompleted(completedId);
        } else {
            break; // Commands complete in order
        }
    }

    if (m_queue.isEmpty()) {
        emit allCommandsCompleted();
    }
}
