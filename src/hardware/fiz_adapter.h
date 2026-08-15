#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — FIZ Device Adapter
// ═══════════════════════════════════════════════════════════════════════════════

#include "hardware/device_adapter.h"
#include "infrastructure/fiz/nucleusservice.h"
#include "core/types.h"

namespace hardware {

class FizAdapter : public IDeviceAdapter {
    Q_OBJECT
public:
    explicit FizAdapter(NucleusService* service, QObject* parent = nullptr)
        : IDeviceAdapter(parent), m_service(service) 
    {}

    QString deviceName() const override { return "Tilta Nucleus-M"; }
    
    bool isReady() const override {
        return m_service && m_service->isConnected();
    }

    bool isConnected() const override {
        return m_service && m_service->isConnected();
    }

    void enqueueMoveCommand(const QVariant& target, double /*expectedDurationSec*/) override {
        if (!m_service || !m_service->isConnected()) return;
        
        if (target.canConvert<FizState>()) {
            FizState state = target.value<FizState>();
            // Note: FIZ motors don't have a "speed" parameter in the nucleus protocol we are using, 
            // they just move as fast as possible to the target in step mode.
            m_service->sendFizFrame(state);
        } else {
            emit errorOccurred("Invalid target format for FIZ");
        }
    }

    void sendStreamedSetpoint(const QVariant& setpoint) override {
        if (!m_service || !m_service->isConnected()) return;
        
        if (setpoint.canConvert<FizState>()) {
            FizState state = setpoint.value<FizState>();
            m_service->sendFizFrame(state);
        }
    }

    void stopMotion() override {
        // FIZ step motion can't easily be aborted mid-flight in the current protocol,
        // but we ensure we don't send any more.
    }

    void emergencyStop() override {
        // FIZ doesn't have a strict E-STOP in our protocol implementation yet,
        // we can just stop sending new targets.
    }

private:
    NucleusService* m_service;
};

} // namespace hardware
