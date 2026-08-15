// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Realtime Feedback Worker
// ═══════════════════════════════════════════════════════════════════════════════

#include "network/realtime_feedback_worker.h"
#include <QDebug>

RealtimeFeedbackWorker::RealtimeFeedbackWorker(QObject* parent)
    : QObject(parent)
    , m_buffer(FeedbackParser::PACKET_SIZE)
{
    m_socket = new QTcpSocket(this);

    connect(m_socket, &QTcpSocket::connected,
            this, &RealtimeFeedbackWorker::onConnected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &RealtimeFeedbackWorker::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &RealtimeFeedbackWorker::onSocketError);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &RealtimeFeedbackWorker::onReadyRead);
}

RealtimeFeedbackWorker::~RealtimeFeedbackWorker()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
        m_socket->waitForDisconnected(1000);
    }
}

bool RealtimeFeedbackWorker::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void RealtimeFeedbackWorker::connectToRobot(const QString& ip, quint16 port)
{
    if (isConnected()) {
        qDebug() << "FeedbackWorker: Already connected";
        return;
    }

    m_buffer.clear();
    qDebug() << "FeedbackWorker: Connecting to" << ip << ":" << port;
    m_socket->connectToHost(ip, port);
}

void RealtimeFeedbackWorker::disconnectFromRobot()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
    m_buffer.clear();
}

void RealtimeFeedbackWorker::onConnected()
{
    qDebug() << "FeedbackWorker: Connected to port 30004";
    m_buffer.clear();
    emit connected();
    emit connectionStateChanged(true);
}

void RealtimeFeedbackWorker::onDisconnected()
{
    qDebug() << "FeedbackWorker: Disconnected";
    m_buffer.clear();
    emit disconnected();
    emit connectionStateChanged(false);
}

void RealtimeFeedbackWorker::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    QString errStr = m_socket->errorString();
    qWarning() << "FeedbackWorker: Socket error:" << errStr;
    emit errorOccurred(errStr);
}

void RealtimeFeedbackWorker::onReadyRead()
{
    // Read all available bytes into the accumulator
    QByteArray incoming = m_socket->readAll();
    m_buffer.append(incoming);

    // Extract all complete 1440-byte frames
    while (m_buffer.hasFrame()) {
        QByteArray frame = m_buffer.extractOneFrame();
        FeedbackData fd = FeedbackParser::parse(frame);

        if (fd.valid) {
            // Emit compact signal for high-frequency consumers
            emit feedbackReceived(
                fd.actualJoints,
                fd.actualPose,
                fd.robotMode(),
                fd.digitalInputs,
                fd.digitalOutputs
            );

            // Emit full data for status bar / diagnostics
            emit fullFeedbackReceived(fd);
        }
    }
}
