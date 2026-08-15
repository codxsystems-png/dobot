#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Dobot TCP Client (Port 29999 Dashboard)
// QTcpSocket wrapper for ASCII command/response communication.
// Lives on robotThread via moveToThread().
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include "core/response_parser.h"

class DobotTcpClient : public QObject
{
    Q_OBJECT
public:
    explicit DobotTcpClient(QObject* parent = nullptr);
    ~DobotTcpClient() override;

    /// Connection state
    virtual bool isConnected() const;

public slots:
    /// Connect to Dobot dashboard port
    void connectToRobot(const QString& ip, quint16 port = 29999);

    /// Disconnect
    void disconnectFromRobot();

    /// Send an ASCII command and wait for response (blocking within thread).
    /// timeout in milliseconds. Returns parsed response.
    /// Virtual so tests can substitute a mock transport (see MockDobotTcpClient).
    virtual ParsedResponse sendCommand(const QString& command, int timeoutMs = 5000);

    /// Send a command without waiting for response (fire-and-forget).
    virtual void sendCommandAsync(const QString& command);

signals:
    /// Emitted when connection established
    void connected();

    /// Emitted when disconnected
    void disconnected();

    /// Emitted on socket error
    void errorOccurred(const QString& errorString);

    /// Emitted when a response is received (for async monitoring)
    void responseReceived(const ParsedResponse& response);

    /// Emitted when connection state changes
    void connectionStateChanged(bool connected);

private slots:
    void onConnected();
    void onDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onReadyRead();

private:
    QTcpSocket* m_socket        = nullptr;
    QString     m_pendingData;
    bool        m_waitingForResponse = false;
    QString     m_lastResponse;
};
