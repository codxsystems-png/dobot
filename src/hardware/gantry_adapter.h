#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Gantry Device Adapter
// ═══════════════════════════════════════════════════════════════════════════════

#include "hardware/device_adapter.h"
#include "infrastructure/gantry/gantryaxiscontroller.h"
#include <QMetaObject>

namespace hardware {

class GantryAdapter : public IDeviceAdapter {
    Q_OBJECT
public:
    explicit GantryAdapter(GantryAxisController* controller, QObject* parent = nullptr)
        : IDeviceAdapter(parent), m_controller(controller)
    {}

    QString deviceName() const override { return "Gantry Axis"; }

    bool isReady() const override {
        return m_controller && m_controller->isConnected() && m_controller->isHomed();
    }

    void enqueueMoveCommand(const QVariant& target, double /*expectedDurationSec*/) override {
        if (!m_controller || !m_controller->isConnected()) return;

        bool ok;
        double targetMm = target.toDouble(&ok);
        if (ok) {
            // In Fire-Together mode, we just issue a new tick target and let the internal PID handle it.
            // A more advanced integration would adjust the Gantry speed to match `expectedDurationSec`.
            // GantryAxisController lives on its own thread (see MainWindow::initServices), so this
            // must be marshaled via a queued invoke rather than called directly cross-thread.
            QMetaObject::invokeMethod(m_controller, "tick", Qt::QueuedConnection,
                                       Q_ARG(double, targetMm));
        } else {
            emit errorOccurred("Invalid target format for Gantry");
        }
    }

    void sendStreamedSetpoint(const QVariant& setpoint) override {
        if (!m_controller || !m_controller->isConnected()) return;

        bool ok;
        double targetMm = setpoint.toDouble(&ok);
        if (ok) {
            QMetaObject::invokeMethod(m_controller, "tick", Qt::QueuedConnection,
                                       Q_ARG(double, targetMm));
        }
    }

    void stopMotion() override {
        if (m_controller) {
            // Sends "g <held position>" — queued onto the controller's own thread.
            QMetaObject::invokeMethod(m_controller, "stopJog", Qt::QueuedConnection);
        }
    }

    void emergencyStop() override {
        if (m_controller) {
            QMetaObject::invokeMethod(m_controller, "stopJog", Qt::QueuedConnection);
        }
    }

private:
    GantryAxisController* m_controller;
};

} // namespace hardware
