#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Real QSerialPort-backed transport (Phase 7)
// The only file that touches QSerialPort directly for the gantry link;
// GantryAxisController talks to ISerialTransport, never this class by name.
// Only meaningful when Qt6::SerialPort is available (HAS_SERIALPORT).
// ═══════════════════════════════════════════════════════════════════════════════

#include "infrastructure/gantry/serial_transport.h"

#ifdef HAS_SERIALPORT
#include <QSerialPort>

class QtSerialTransport : public ISerialTransport
{
    Q_OBJECT
public:
    explicit QtSerialTransport(QObject* parent = nullptr);

    bool open(const QString& portName) override;
    void close() override;
    bool isOpen() const override;
    void write(const QByteArray& data) override;
    void flush() override;
    QByteArray readAll() override;
    QString lastErrorString() const override;

private slots:
    void onError(QSerialPort::SerialPortError error);

private:
    QSerialPort* m_serial = nullptr;
};
#endif

/// Returns a QtSerialTransport when Qt6::SerialPort is available, nullptr
/// otherwise — the one place that branches on HAS_SERIALPORT so
/// GantryAxisController doesn't have to.
ISerialTransport* createDefaultSerialTransport(QObject* parent);
