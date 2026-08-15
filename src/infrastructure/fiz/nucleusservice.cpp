// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Nucleus Service
// ═══════════════════════════════════════════════════════════════════════════════

#include "infrastructure/fiz/nucleusservice.h"
#include "infrastructure/fiz/nucleusprotocol.h"
#include <QThread>
#include <QDebug>

NucleusService::NucleusService(QObject* parent)
    : QObject(parent)
{
    m_heartbeat = new QTimer(this);
    m_heartbeat->setInterval(1000);
    connect(m_heartbeat, &QTimer::timeout, this, &NucleusService::sendHeartbeat);
}

NucleusService::~NucleusService()
{
    disconnectPort();
}

QStringList NucleusService::availablePorts() const
{
    QStringList ports;
#ifdef HAS_SERIALPORT
    for (const auto& info : QSerialPortInfo::availablePorts())
        ports << info.portName();
#endif
    return ports;
}

bool NucleusService::isConnected() const
{
    return m_connected;
}

bool NucleusService::connectPort(const QString& portName)
{
#ifdef HAS_SERIALPORT
    disconnectPort();

    m_serial = new QSerialPort(this);
    m_serial->setPortName(portName);
    m_serial->setBaudRate(115200);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    connect(m_serial, &QSerialPort::errorOccurred,
            this, &NucleusService::onSerialError);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        emit errorOccurred("Failed to open " + portName + ": " + m_serial->errorString());
        delete m_serial;
        m_serial = nullptr;
        return false;
    }

    m_connected = true;
    m_heartbeat->start();

    qDebug() << "NucleusService: Connected to" << portName;
    emit connected(portName);
    return true;
#else
    Q_UNUSED(portName)
    emit errorOccurred("Qt6::SerialPort not available — FIZ disabled");
    return false;
#endif
}

void NucleusService::disconnectPort()
{
    m_heartbeat->stop();

#ifdef HAS_SERIALPORT
    if (m_serial) {
        if (m_serial->isOpen())
            m_serial->close();
        delete m_serial;
        m_serial = nullptr;
    }
#endif

    if (m_connected) {
        m_connected = false;
        emit disconnected();
        qDebug() << "NucleusService: Disconnected";
    }
}

void NucleusService::setFocus(float percent)
{
    m_currentState.focus = std::clamp(percent, 0.0f, 100.0f);
    sendPacket(NucleusProtocol::focusCommand(m_currentState.focus));
    emit fizStateChanged(m_currentState);
}

void NucleusService::setIris(float percent)
{
    m_currentState.iris = std::clamp(percent, 0.0f, 100.0f);
    sendPacket(NucleusProtocol::irisCommand(m_currentState.iris));
    emit fizStateChanged(m_currentState);
}

void NucleusService::setZoom(float percent)
{
    m_currentState.zoom = std::clamp(percent, 0.0f, 100.0f);
    sendPacket(NucleusProtocol::zoomCommand(m_currentState.zoom));
    emit fizStateChanged(m_currentState);
}

void NucleusService::sendFizFrame(const FizState& state)
{
    // Send all 3 motors with 2 ms inter-packet delay per spec §4
    m_currentState = state;

    sendPacket(NucleusProtocol::focusCommand(state.focus));
    QThread::msleep(2);
    sendPacket(NucleusProtocol::irisCommand(state.iris));
    QThread::msleep(2);
    sendPacket(NucleusProtocol::zoomCommand(state.zoom));

    emit fizStateChanged(m_currentState);
}

void NucleusService::calibrateMotor(uint8_t motorId)
{
    sendPacket(NucleusProtocol::calibrateCommand(motorId));
    emit motorCalibrated(motorId);
    qDebug() << "NucleusService: Calibrated motor" << motorId;
}

void NucleusService::calibrateAll()
{
    calibrateMotor(NucleusProtocol::MOTOR_FOCUS);
    QThread::msleep(50);
    calibrateMotor(NucleusProtocol::MOTOR_IRIS);
    QThread::msleep(50);
    calibrateMotor(NucleusProtocol::MOTOR_ZOOM);
}

void NucleusService::setLensMapping(const LensMapping& m)
{
    m_lensMapping = m;
}

float NucleusService::focusPercentToMm(float pct) const
{
    float clamped = std::clamp(pct, 0.0f, 100.0f);
    float t = clamped / 100.0f;
    return m_lensMapping.focusNearMm +
           t * (m_lensMapping.focusFarMm - m_lensMapping.focusNearMm);
}

float NucleusService::zoomPercentToMm(float pct) const
{
    float clamped = std::clamp(pct, 0.0f, 100.0f);
    float t = clamped / 100.0f;
    return m_lensMapping.zoomWideMm +
           t * (m_lensMapping.zoomTeleMm - m_lensMapping.zoomWideMm);
}

void NucleusService::sendHeartbeat()
{
    sendPacket(NucleusProtocol::heartbeatPacket());
}

#ifdef HAS_SERIALPORT
void NucleusService::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) return;
    emit errorOccurred("Serial error: " + m_serial->errorString());
    if (error == QSerialPort::ResourceError) {
        disconnectPort();
    }
}
#endif

void NucleusService::sendPacket(const QByteArray& packet)
{
#ifdef HAS_SERIALPORT
    if (!m_serial || !m_serial->isOpen()) return;
    m_serial->write(packet);
    m_serial->flush();
#else
    Q_UNUSED(packet)
#endif
}
