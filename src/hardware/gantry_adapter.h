#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Gantry Device Adapter
// ═══════════════════════════════════════════════════════════════════════════════

#include "hardware/device_adapter.h"
#include "infrastructure/gantry/axis_controller_base.h"
#include <QMetaObject>

namespace hardware {

class GantryAdapter : public IDeviceAdapter {
    Q_OBJECT
public:
    explicit GantryAdapter(AxisControllerBase* controller, QObject* parent = nullptr)
        : IDeviceAdapter(parent), m_controller(controller)
    {}

    QString deviceName() const override { return "Gantry Axis"; }

    /// Re-points the adapter at a different axis controller.
    ///
    /// The external axis can be a DC servo or a stepper, and switching between
    /// them swaps the controller under a LIVE adapter — the adapter itself is
    /// created once and registered with the playback engine under "gantry".
    /// Without this it keeps streaming setpoints to the axis we stopped
    /// driving, which looks exactly like playback doing nothing: position
    /// updates keep arriving from the selected axis while the moves go to the
    /// other one.
    void setController(AxisControllerBase* controller) { m_controller = controller; }
    AxisControllerBase* controller() const { return m_controller; }

    bool isReady() const override {
        return m_controller && m_controller->isConnected() && m_controller->isHomed();
    }

    bool isConnected() const override {
        return m_controller && m_controller->isConnected();
    }

    void enqueueMoveCommand(const QVariant& target, double /*expectedDurationSec*/) override {
        if (!m_controller || !m_controller->isConnected()) return;

        bool ok;
        double targetMm = target.toDouble(&ok);
        if (ok) {
            // In Fire-Together mode, we just issue a new tick target and let the internal PID handle it.
            // A more advanced integration would adjust the Gantry speed to match `expectedDurationSec`.
            // The axis controller lives on its own thread (see MainWindow::initServices), so this
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
    AxisControllerBase* m_controller;
};

} // namespace hardware
