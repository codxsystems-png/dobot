#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Nucleus Service (Phase 7)
// QSerialPort wrapper. Runs on its own QThread.
// Heartbeat timer (1000 ms) keeps motors engaged.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QTimer>
#include "core/types.h"

#ifdef HAS_SERIALPORT
#include <QSerialPort>
#include <QSerialPortInfo>
#endif

class NucleusService : public QObject
{
    Q_OBJECT
public:
    explicit NucleusService(QObject* parent = nullptr);
    ~NucleusService() override;

    QStringList availablePorts() const;
    bool isConnected() const;
    FizState currentState() const { return m_currentState; }
    LensMapping lensMapping() const { return m_lensMapping; }

public slots:
    bool connectPort(const QString& portName);
    void disconnectPort();

    void setFocus(float percent);
    void setIris(float percent);
    void setZoom(float percent);
    void sendFizFrame(const FizState& state);

    void calibrateMotor(uint8_t motorId);
    void calibrateAll();

    void setLensMapping(const LensMapping& m);
    float focusPercentToMm(float pct) const;
    float zoomPercentToMm(float pct) const;

signals:
    void connected(const QString& portName);
    void disconnected();
    void errorOccurred(const QString& message);
    void fizStateChanged(const FizState& state);
    void motorCalibrated(uint8_t motorId);

private slots:
    void sendHeartbeat();
#ifdef HAS_SERIALPORT
    void onSerialError(QSerialPort::SerialPortError error);
#endif

private:
    void sendPacket(const QByteArray& packet);

#ifdef HAS_SERIALPORT
    QSerialPort* m_serial     = nullptr;
#endif
    QTimer*      m_heartbeat  = nullptr;
    FizState     m_currentState;
    LensMapping  m_lensMapping;
    bool         m_connected  = false;
};
