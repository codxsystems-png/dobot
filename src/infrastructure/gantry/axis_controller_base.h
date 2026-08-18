#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Axis Controller Base
//
// What every external motion axis has in common, regardless of how it is
// driven: a board link and an axis address on it, a calibration factor, travel
// limits, a homed flag, a current position, and a 50Hz control loop.
//
// What it deliberately does NOT assume is the control model. A DC servo runs a
// host-side PID over PWM and reads an encoder; a CL57C stepper is handed
// step targets and closes its own loop internally. Those are different enough
// that the loop itself is a pure virtual rather than a branch — which makes
// "steppers have no PID to tune" a fact of the type system instead of a
// convention someone has to remember.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QStringList>
#include <QTimer>
#include "core/types.h"
#include "core/axis_protocol.h"
#include "infrastructure/gantry/axis_board_link.h"
#include "infrastructure/gantry/serial_transport.h"
#include "services/connection_state_machine.h"

class AxisControllerBase : public QObject, public IAxisReplyHandler
{
    Q_OBJECT
public:
    /// Shares an existing board link with the other axes on that board.
    /// The link must outlive this controller.
    AxisControllerBase(AxisBoardLink* link, int axisIndex, QObject* parent = nullptr);

    /// Builds and owns a private link over `transport` — for a single-axis rig
    /// and for tests. transport == nullptr uses the real QSerialPort-backed
    /// one, or a no-op when Qt6::SerialPort is absent.
    explicit AxisControllerBase(ISerialTransport* transport = nullptr, QObject* parent = nullptr);

    ~AxisControllerBase() override;

    QStringList availablePorts() const;

    /// The port is open. NOT sufficient to command motion — see isIdentified().
    bool isConnected() const;

    /// The board answered the version handshake compatibly. Motion is gated on
    /// this: opening the port asserts DTR, which reboots an Uno and leaves
    /// ~1.6s where no sketch is running to receive anything.
    bool isIdentified() const;

    bool   isHomed() const { return m_isHomed; }
    double currentPositionMm() const { return m_currentPositionMm; }
    int    axisIndex() const { return m_axisIndex; }

    /// Encoder counts (DC) or steps (stepper) per unit of axis travel.
    /// One physical constant, one field — the meaning follows the drive kind.
    void   setEncoderCountsPerMm(double countsPerUnit);
    double encoderCountsPerMm() const { return m_countsPerMm; }

    /// Runtime safety net: every target is clamped to this range before it
    /// reaches the loop, independent of whatever preflight the caller did.
    void         setTravelLimits(const GantryLimits& limits) { m_travelLimits = limits; }
    GantryLimits travelLimits() const { return m_travelLimits; }

    ConnectionStateMachine::State connectionState() const;

public slots:
    bool connectPort(const QString& portName);
    void disconnectPort();

    /// Apply the persisted configuration. The base handles what every axis
    /// shares; subclasses extend it with their own (PID gains, step limits).
    virtual void applyTuning(const GantryTuning& tuning);

    // ─── The control model itself ─────────────────────────────────────────
    /// One streamed setpoint, in axis units. Called at 50Hz during playback.
    virtual void tick(double targetUnits) = 0;
    /// One control tick when playback isn't driving — polling, homing, etc.
    virtual void heartbeat() = 0;
    virtual void homeGantry() = 0;
    virtual void jogGantry(int speed) = 0;
    virtual void stopJog() = 0;
    virtual void resetEncoder() = 0;

signals:
    void connected(const QString& portName);
    void disconnected();
    void errorOccurred(const QString& message);
    void positionChanged(double positionUnits);
    void homed();

protected:
    /// Clamps to the travel limits, logging and reporting when it had to.
    /// Shared so every axis kind reports an out-of-range target identically.
    double clampToTravel(double targetUnits);

    /// Called by onLinkLost() BEFORE the base tears the loop down, so a
    /// subclass can cancel whatever it had in flight while a stop command can
    /// still reach the board.
    virtual void onLinkLostImpl() {}

    /// Called by connectPort() before the port is opened. A subclass clears
    /// whatever control state it carries, so a reconnect never resumes a
    /// half-finished move or a stale output from the previous session.
    virtual void resetControlState() {}

    void onLinkLost() final;

    AxisBoardLink* m_link      = nullptr;
    bool           m_ownsLink  = false;
    int            m_axisIndex = 0;

    QTimer* m_controlTimer = nullptr;

    bool   m_isHomed = false;
    double m_countsPerMm = 100.0;
    double m_currentPositionMm = 0.0;
    GantryLimits m_travelLimits;

private:
    void wireLink();
};
