#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Device Adapter Interface
// Playback Engine decoupling layer.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QVariant>
#include <QString>
#include <QObject>

namespace hardware {

class IDeviceAdapter : public QObject {
    Q_OBJECT
public:
    explicit IDeviceAdapter(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IDeviceAdapter() = default;

    virtual QString deviceName() const = 0;

    // Returns true if the device is ready to accept commands
    virtual bool isReady() const = 0;

    // Returns true if the underlying transport is connected at all — NOT
    // the same as isReady(): a disconnected device is also "not ready", but
    // callers that need to tell "genuinely mid-move" apart from "not even
    // connected" (e.g. deciding whether to hold off ending playback) need
    // this distinction. See PlaybackEngine::onTick()'s end-of-timeline and
    // pause-after-move checks — treating "not connected" the same as
    // "still moving" meant playback could never stop on its own whenever a
    // device simply wasn't hooked up.
    virtual bool isConnected() const = 0;

    // Mode 1: Fire-Together (send to target, device handles profile internally)
    virtual void enqueueMoveCommand(const QVariant& target, double expectedDurationSec) = 0;

    // Mode 2: Streamed (continuous position setpoints)
    virtual void sendStreamedSetpoint(const QVariant& setpoint) = 0;

    // Graceful stop (used for Pause/Stop)
    virtual void stopMotion() = 0;

    // Safety (used for E-STOP)
    virtual void emergencyStop() = 0;
    
signals:
    void commandCompleted(int id);
    void errorOccurred(const QString& msg);
};

} // namespace hardware
