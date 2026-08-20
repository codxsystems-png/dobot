#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Stepper Axis Controller
//
// A CL57C closed-loop stepper. The drive closes its own loop against its
// internal encoder, so the host runs no PID at all: it streams absolute step
// targets and the board's Timer1 clocks pulses toward them.
//
// This is the only axis control model the rig has. There is no host-side PID,
// no gains and nothing to tune: the drive does that internally. If a servo is
// fitted later it gets an encoder and the SAME logic, rather than a second
// control model living alongside this one.
//
// THE THING TO UNDERSTAND ABOUT THIS AXIS: the host's position is what it
// ASKED FOR, not what happened. A missed step is invisible from here — Q
// reports the pulses the board clocked out, not the shaft. The CL57C's ALM
// output is the only integrity signal this axis has, which is why an alarm
// clears the homed flag rather than merely being reported.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QElapsedTimer>
#include "core/types.h"
#include "core/axis_protocol.h"
#include "infrastructure/gantry/axis_controller_base.h"

class StepperAxisController : public AxisControllerBase
{
    Q_OBJECT
public:
    /// Shares an existing board link with the other axes on that board.
    StepperAxisController(AxisBoardLink* link, int axisIndex, QObject* parent = nullptr);

    /// Convenience for a single-axis rig (and for tests): owns a private link.
    explicit StepperAxisController(ISerialTransport* transport = nullptr, QObject* parent = nullptr);

    /// Reply routing from the shared link (IAxisReplyHandler).
    void onReply(const axisproto::Reply& reply) override;

    /// Step-rate clamps enforced by the board, in steps/s and steps/s².
    ///
    /// The acceleration matters more than it looks: the board ramps its rate
    /// by amax x its follower period, and it cannot clock anything slower than
    /// its own floor. Very low values make an axis that starts sluggishly;
    /// see the firmware README before going below a few thousand.
    void setStepRateLimits(long vmaxStepsPerSec, long amaxStepsPerSec2);
    long vmaxStepsPerSec() const { return m_vmax; }
    long amaxStepsPerSec2() const { return m_amax; }

    /// Whether to drop ENABLE when the axis is idle. Defaults to false, and
    /// should stay false on anything gravity-loaded: ENABLE is a torque
    /// switch, and releasing it makes a loaded axis fall.
    void setIdleDisable(bool on) { m_idleDisable = on; }
    bool idleDisable() const { return m_idleDisable; }

    bool isEnabled()  const { return m_enabled; }
    bool isAlarmed()  const { return m_alarmed; }
    bool isHalted()   const { return m_halted; }

    /// Where the board says it is, in steps.
    long boardSteps() const { return m_boardSteps; }
    /// The last target this controller commanded, in steps.
    long commandedSteps() const { return m_targetSteps; }

public slots:
    void applyTuning(const GantryTuning& tuning) override;

    // ─── The stepper control model (AxisControllerBase) ───────────────────
    void tick(double targetUnits) override;
    void heartbeat() override;

    /// "Set Zero Here". This rig has no home switch on the stepper axis, so
    /// referencing is declaring the current position to be the origin.
    ///
    /// The consequence to accept is that the origin moves every session and
    /// the travel limits are relative to wherever it was zeroed. The firmware
    /// still implements a real H/homing sequence for when a switch is fitted.
    void homeGantry() override;

    /// Signed steps/s. Note this differs in UNITS from the DC axis's jog,
    /// which is PWM — the base declares the slot, each drive kind means its
    /// own thing by "speed".
    void jogGantry(int stepsPerSec) override;
    void stopJog() override;
    void resetEncoder() override;

    /// Torque. Deliberate operator action only — see setIdleDisable().
    void setEnabled(bool on);

    /// Clears a latched fault on the board (R). The board adopts its current
    /// position as the target when it does, so a resumed link can never
    /// continue a move the operator has lost track of.
    void clearFault();

signals:
    /// The drive asserted ALM. Position is no longer trustworthy: this also
    /// clears the homed flag, which every downstream consumer already gates on.
    void alarmRaised(const QString& text);

    /// The board's step count has stopped converging on the commanded target
    /// while a gap remains. Means the board is halted, disabled, or not
    /// receiving setpoints — NOT that the motor is losing steps, which is
    /// invisible from here and only ALM can catch.
    void driveStalled(long commandedSteps, long boardSteps);

private:
    void sendTarget();
    void sendLimits();
    long unitsToSteps(double units) const;
    double stepsToUnits(long steps) const;

    enum class State { Idle, Tracking, Jogging };
    State m_state = State::Idle;

    void onLinkLostImpl() override;
    void resetControlState() override;
    void onIdentified() override;

    long m_targetSteps = 0;
    long m_boardSteps  = 0;
    int  m_jogRate     = 0;

    long m_vmax = 8000;
    long m_amax = 40000;
    bool m_idleDisable = false;

    bool m_enabled = false;
    bool m_alarmed = false;
    bool m_halted  = false;

    // Position polling. Unlike the DC axis there is no loop depending on a
    // fresh reading every tick, so this is diagnostic rather than structural
    // and is polled at a fraction of the tick rate to leave the link quiet.
    int m_ticksSincePoll = 0;
    static constexpr int POLL_EVERY_TICKS = 5;   // 100ms at a 20ms tick
    /// Set while tick() is refusing, so the reason is reported once rather
    /// than at the streaming rate.
    bool m_refusalLogged = false;
    int m_pollsSinceStatus = 0;
    static constexpr int STATUS_EVERY_POLLS = 10;  // ~1s

    // Stall detection: consecutive polls where the board neither reached the
    // target nor moved toward it at all.
    long m_lastPolledSteps = 0;
    int  m_stalledPolls    = 0;
    bool m_stallReported   = false;
    static constexpr int STALL_POLLS = 10;       // ~1s of no progress
    static constexpr long STALL_TOLERANCE_STEPS = 2;
};
