#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Mock FIZ Adapter (Phase 1: simulation layer)
// Accepts FizState setpoints instantly (matching the real Nucleus-M protocol,
// which has no speed parameter) so timeline/FIZ logic is testable without a
// physical Tilta Nucleus-M attached.
// ═══════════════════════════════════════════════════════════════════════════════

#include "hardware/device_adapter.h"
#include "core/types.h"

namespace hardware {

class MockFizAdapter : public IDeviceAdapter {
    Q_OBJECT
public:
    explicit MockFizAdapter(QObject* parent = nullptr) : IDeviceAdapter(parent) {}

    QString deviceName() const override { return "Mock Tilta Nucleus-M"; }

    bool isReady() const override { return m_ready; }
    bool isConnected() const override { return m_connected; }

    /// Test hook: simulate the adapter being disconnected.
    void setReady(bool ready) { m_ready = ready; }
    void setConnected(bool connected) { m_connected = connected; }

    FizState lastState() const { return m_lastState; }

    void enqueueMoveCommand(const QVariant& target, double /*expectedDurationSec*/) override
    {
        sendStreamedSetpoint(target);
    }

    void sendStreamedSetpoint(const QVariant& setpoint) override
    {
        if (!m_ready) {
            emit errorOccurred("MockFizAdapter: not ready");
            return;
        }
        if (!setpoint.canConvert<FizState>()) {
            emit errorOccurred("MockFizAdapter: invalid target payload");
            return;
        }

        m_lastState = setpoint.value<FizState>();
        emit commandCompleted(++m_nextId);
    }

    void stopMotion() override {}
    void emergencyStop() override {}

private:
    bool m_ready     = true;
    bool m_connected = true;
    int  m_nextId = 0;
    FizState m_lastState{};
};

} // namespace hardware
