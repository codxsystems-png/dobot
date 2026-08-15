#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Mock Dobot Adapter (Phase 1: simulation layer)
// Simulates move timing via TrapezoidalProfile so PlaybackEngine/timeline logic
// can be built and tested without a physical Dobot Nova 5 attached.
// ═══════════════════════════════════════════════════════════════════════════════

#include "hardware/device_adapter.h"
#include "hardware/dobot_adapter.h"
#include "math/motion_profile.h"
#include <QTimer>
#include <QtGlobal>
#include <cmath>

namespace hardware {

class MockDobotAdapter : public IDeviceAdapter {
    Q_OBJECT
public:
    explicit MockDobotAdapter(QObject* parent = nullptr) : IDeviceAdapter(parent) {}

    QString deviceName() const override { return "Mock Dobot Nova 5"; }

    bool isReady() const override { return m_ready && !m_moving; }
    bool isConnected() const override { return m_connected; }

    /// Test hook: simulate the adapter being disconnected/not-idle.
    void setReady(bool ready) { m_ready = ready; }

    /// Test hook: simulate the transport itself being connected or not —
    /// distinct from isReady() (busy-but-connected vs. not connected at all).
    void setConnected(bool connected) { m_connected = connected; }

    /// Test hook: seed the simulated robot's starting pose (default: origin).
    void setCurrentPose(const CartesianPose& pose) { m_currentPose = pose; }
    CartesianPose currentPose() const { return m_currentPose; }

    bool isMoving() const { return m_moving; }

    void enqueueMoveCommand(const QVariant& target, double expectedDurationSec) override
    {
        if (!m_ready) {
            emit errorOccurred("MockDobotAdapter: not ready");
            return;
        }
        if (!target.canConvert<DobotMoveTarget>()) {
            emit errorOccurred("MockDobotAdapter: invalid target payload");
            return;
        }

        DobotMoveTarget tgt = target.value<DobotMoveTarget>();
        double durationSec = expectedDurationSec > 0.0
            ? expectedDurationSec
            : simulatedDuration(tgt);

        int id = ++m_nextId;
        m_moving = true;
        m_pendingTarget = tgt.targetPose;

        QTimer::singleShot(qMax(1, int(durationSec * 1000.0)), this, [this, id]() {
            m_moving = false;
            m_currentPose = m_pendingTarget;
            emit commandCompleted(id);
        });
    }

    void sendStreamedSetpoint(const QVariant& setpoint) override
    {
        if (!m_ready) {
            emit errorOccurred("MockDobotAdapter: not ready");
            return;
        }
        if (!setpoint.canConvert<DobotMoveTarget>()) {
            emit errorOccurred("MockDobotAdapter: invalid streamed setpoint payload");
            return;
        }
        m_lastStreamedTarget = setpoint.value<DobotMoveTarget>();
        m_currentPose = m_lastStreamedTarget.targetPose; // streamed setpoints apply instantly in sim
        ++m_streamedSetpointCount;
    }

    /// Test hook: how many streamed setpoints has this adapter received.
    int streamedSetpointCount() const { return m_streamedSetpointCount; }
    DobotMoveTarget lastStreamedTarget() const { return m_lastStreamedTarget; }

    void stopMotion() override { m_moving = false; }
    void emergencyStop() override { m_moving = false; }

private:
    // Rough stand-ins for a Nova 5's max Cartesian velocity/acceleration, used only to
    // give simulated moves a physically-plausible duration when the caller doesn't
    // supply one. Not calibrated against real hardware — mock timing only.
    static constexpr double kBaseVelocityMmPerSec = 400.0;
    static constexpr double kBaseAccelMmPerSec2   = 800.0;
    static constexpr double kMinMoveDurationSec   = 0.05;

    double simulatedDuration(const DobotMoveTarget& tgt) const
    {
        double dx = tgt.targetPose.x - m_currentPose.x;
        double dy = tgt.targetPose.y - m_currentPose.y;
        double dz = tgt.targetPose.z - m_currentPose.z;
        double distanceMm = std::sqrt(dx * dx + dy * dy + dz * dz);

        double speedFrac = qBound(1, tgt.speedPct, 100) / 100.0;
        double accFrac   = qBound(1, tgt.accPct, 100) / 100.0;
        double vMax = kBaseVelocityMmPerSec * speedFrac;
        double aMax = kBaseAccelMmPerSec2 * accFrac;

        if (distanceMm < 1e-6) {
            return kMinMoveDurationSec;
        }

        math::TrapezoidalProfile profile(0.0, distanceMm, vMax, aMax);
        return qMax(kMinMoveDurationSec, profile.duration());
    }

    bool m_ready     = true;
    bool m_connected = true;
    bool m_moving    = false;
    int  m_nextId = 0;
    CartesianPose m_currentPose{};
    CartesianPose m_pendingTarget{};
    int m_streamedSetpointCount = 0;
    DobotMoveTarget m_lastStreamedTarget{};
};

} // namespace hardware
