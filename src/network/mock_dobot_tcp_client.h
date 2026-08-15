#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Mock Dobot TCP Client (Phase 1: simulation layer)
// Drives CommandQueueManager with scripted ResultID/error sequences so the
// queue-polling logic can be tested without a real socket or robot attached.
// ═══════════════════════════════════════════════════════════════════════════════

#include "network/dobot_tcp_client.h"
#include <QQueue>

class MockDobotTcpClient : public DobotTcpClient
{
public:
    explicit MockDobotTcpClient(QObject* parent = nullptr) : DobotTcpClient(parent) {}

    void setConnected(bool connected) { m_mockConnected = connected; }

    /// Script the response to the next non-GetCurrentCommandID sendCommand() call.
    void queueResponse(int errorId, int resultId)
    {
        m_responses.enqueue({errorId, resultId});
    }

    /// What GetCurrentCommandID() should report on the next poll.
    void setCurrentCommandId(int id) { m_currentCommandId = id; }

    bool isConnected() const override { return m_mockConnected; }

    ParsedResponse sendCommand(const QString& command, int /*timeoutMs*/ = 5000) override
    {
        ParsedResponse resp;
        resp.rawResponse = command;
        resp.commandName = command;
        resp.valid = true;

        if (command.startsWith(QStringLiteral("GetCurrentCommandID"))) {
            resp.errorId = 0;
            resp.values << QString::number(m_currentCommandId);
            return resp;
        }

        if (!m_mockConnected) {
            resp.valid = false;
            return resp;
        }

        if (m_responses.isEmpty()) {
            // No response scripted — auto-assign the next sequential ResultID.
            resp.errorId = 0;
            resp.values << QString::number(++m_autoResultId);
            return resp;
        }

        const auto [errorId, resultId] = m_responses.dequeue();
        resp.errorId = errorId;
        resp.values << QString::number(resultId);
        return resp;
    }

    void sendCommandAsync(const QString& /*command*/) override {}

private:
    struct ScriptedResponse { int errorId; int resultId; };

    bool m_mockConnected = true;
    int  m_currentCommandId = 0;
    int  m_autoResultId = 0;
    QQueue<ScriptedResponse> m_responses;
};
