#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Realtime Feedback Worker (Port 30004)
// Runs on feedbackThread via moveToThread().
// Reads binary 1440-byte packets, parses, and emits signals at ~125Hz.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QTcpSocket>
#include "core/feedback_parser.h"
#include "core/byte_stream_buffer.h"

class RealtimeFeedbackWorker : public QObject
{
    Q_OBJECT
public:
    explicit RealtimeFeedbackWorker(QObject* parent = nullptr);
    ~RealtimeFeedbackWorker() override;

    bool isConnected() const;

public slots:
    /// Connect to robot feedback port
    void connectToRobot(const QString& ip, quint16 port = 30004);

    /// Disconnect
    void disconnectFromRobot();

signals:
    /// Emitted for each complete feedback frame (~125Hz)
    void feedbackReceived(const JointAngles& joints,
                          const CartesianPose& pose,
                          RobotMode mode,
                          uint64_t digitalInputs,
                          uint64_t digitalOutputs);

    /// Emitted with full parsed data (for status bar, diagnostics)
    void fullFeedbackReceived(const FeedbackData& data);

    /// Connection state
    void connected();
    void disconnected();
    void errorOccurred(const QString& errorString);
    void connectionStateChanged(bool connected);

private slots:
    void onConnected();
    void onDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onReadyRead();

private:
    QTcpSocket*      m_socket = nullptr;
    ByteStreamBuffer m_buffer;
};
