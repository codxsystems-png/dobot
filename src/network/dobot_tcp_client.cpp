// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Dobot TCP Client
// ═══════════════════════════════════════════════════════════════════════════════

#include "network/dobot_tcp_client.h"
#include <QEventLoop>
#include <QDebug>

DobotTcpClient::DobotTcpClient(QObject* parent)
    : QObject(parent)
{
    m_socket = new QTcpSocket(this);

    connect(m_socket, &QTcpSocket::connected,
            this, &DobotTcpClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &DobotTcpClient::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &DobotTcpClient::onSocketError);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &DobotTcpClient::onReadyRead);
}

DobotTcpClient::~DobotTcpClient()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
        m_socket->waitForDisconnected(1000);
    }
}

bool DobotTcpClient::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void DobotTcpClient::connectToRobot(const QString& ip, quint16 port)
{
    if (isConnected()) {
        qDebug() << "DobotTcpClient: Already connected";
        return;
    }

    qDebug() << "DobotTcpClient: Connecting to" << ip << ":" << port;
    m_socket->connectToHost(ip, port);
}

void DobotTcpClient::disconnectFromRobot()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

ParsedResponse DobotTcpClient::sendCommand(const QString& command, int timeoutMs)
{
    ParsedResponse result;

    if (!isConnected()) {
        qWarning() << "DobotTcpClient: Not connected, cannot send:" << command.trimmed();
        return result;
    }

    // Clear any pending data
    m_pendingData.clear();
    m_lastResponse.clear();
    m_waitingForResponse = true;

    // Send the command
    QByteArray data = command.toUtf8();
    m_socket->write(data);
    m_socket->flush();

    qDebug() << "DobotTcpClient TX:" << command.trimmed();

    // Wait for response using a local event loop with timeout
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    // Exit loop when we get a complete response (ends with ';')
    auto conn1 = connect(this, &DobotTcpClient::responseReceived, &loop, [&loop](const ParsedResponse&) {
        loop.quit();
    });
    auto conn2 = connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    auto conn3 = connect(m_socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);

    timer.start(timeoutMs);
    loop.exec();

    disconnect(conn1);
    disconnect(conn2);
    disconnect(conn3);

    m_waitingForResponse = false;

    if (!timer.isActive()) {
        qWarning() << "DobotTcpClient: Response timeout for:" << command.trimmed();
        return result;
    }

    // Parse the accumulated response
    result = ResponseParser::parse(m_lastResponse);
    qDebug() << "DobotTcpClient RX:" << m_lastResponse.trimmed()
             << "ErrorID:" << result.errorId;

    return result;
}

void DobotTcpClient::sendCommandAsync(const QString& command)
{
    if (!isConnected()) {
        qWarning() << "DobotTcpClient: Not connected, cannot send:" << command.trimmed();
        return;
    }

    m_socket->write(command.toUtf8());
    m_socket->flush();
    qDebug() << "DobotTcpClient TX (async):" << command.trimmed();
}

void DobotTcpClient::onConnected()
{
    qDebug() << "DobotTcpClient: Connected";
    emit connected();
    emit connectionStateChanged(true);
}

void DobotTcpClient::onDisconnected()
{
    qDebug() << "DobotTcpClient: Disconnected";
    m_pendingData.clear();
    emit disconnected();
    emit connectionStateChanged(false);
}

void DobotTcpClient::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    QString errStr = m_socket->errorString();
    qWarning() << "DobotTcpClient: Socket error:" << errStr;
    emit errorOccurred(errStr);
}

void DobotTcpClient::onReadyRead()
{
    QByteArray incoming = m_socket->readAll();
    m_pendingData.append(QString::fromUtf8(incoming));

    // Dobot responses end with ';'
    // Accumulate until we see the terminator
    int semiPos = m_pendingData.indexOf(';');
    if (semiPos >= 0) {
        m_lastResponse = m_pendingData.left(semiPos + 1);
        m_pendingData  = m_pendingData.mid(semiPos + 1);

        ParsedResponse parsed = ResponseParser::parse(m_lastResponse);
        emit responseReceived(parsed);
    }
}
