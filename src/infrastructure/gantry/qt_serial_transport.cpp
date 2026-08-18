// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Real QSerialPort-backed transport
// ═══════════════════════════════════════════════════════════════════════════════

#include "infrastructure/gantry/qt_serial_transport.h"

#ifdef HAS_SERIALPORT

QtSerialTransport::QtSerialTransport(QObject* parent)
    : ISerialTransport(parent)
{
    m_serial = new QSerialPort(this);
    // Must match firmware/cambot_axis_v2/cambot_axis_v2.ino. 9600 cannot
    // carry two axes: one character costs 1.04ms there, so a single 20ms
    // tick's traffic for two axes exceeds the tick itself.
    m_serial->setBaudRate(115200);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    connect(m_serial, &QSerialPort::readyRead, this, &ISerialTransport::readyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, &QtSerialTransport::onError);
}

bool QtSerialTransport::open(const QString& portName)
{
    m_serial->setPortName(portName);
    return m_serial->open(QIODevice::ReadWrite);
}

void QtSerialTransport::close()
{
    m_serial->close();
}

bool QtSerialTransport::isOpen() const
{
    return m_serial->isOpen();
}

void QtSerialTransport::write(const QByteArray& data)
{
    m_serial->write(data);
}

void QtSerialTransport::flush()
{
    m_serial->flush();
}

QByteArray QtSerialTransport::readAll()
{
    return m_serial->readAll();
}

QString QtSerialTransport::lastErrorString() const
{
    return m_serial->errorString();
}

void QtSerialTransport::onError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) return;
    emit errorOccurred(m_serial->errorString(), error == QSerialPort::ResourceError);
}

ISerialTransport* createDefaultSerialTransport(QObject* parent)
{
    return new QtSerialTransport(parent);
}

#else

ISerialTransport* createDefaultSerialTransport(QObject* /*parent*/)
{
    return nullptr;
}

#endif
