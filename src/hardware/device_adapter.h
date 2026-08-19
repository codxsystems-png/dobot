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

    /// True when this device's in-flight motion should hold up end-of-
    /// timeline and the pause-after-move check.
    ///
    /// Only devices given discrete commands that take real time to execute
    /// gate: the engine has genuinely handed them something and cannot know
    /// when it lands without asking. A continuously-streamed axis never
    /// gates, because its commanded position IS the setpoint the engine just
    /// sent — there is nothing outstanding to wait for.
    ///
    /// Default false, so an axis that does not opt in can never wedge
    /// playback open. That direction of default matters: the previous
    /// hardcoded version treated a NOT-CONNECTED robot as "still moving",
    /// and playback on a gantry-only rig could never stop by itself.
    virtual bool gatesPlaybackCompletion() const { return false; }

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
