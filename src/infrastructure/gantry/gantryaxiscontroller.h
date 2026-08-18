#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Gantry Axis Controller
// Infrastructure-layer wrapper for Arduino gantry serial protocol.
// Runs on its own thread, driven by external ticks for closed-loop control.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QStringList>
#include <QByteArray>
#include <QTimer>
#include <QElapsedTimer>
#include <algorithm>
#include "core/types.h"
#include "core/axis_protocol.h"
#include "infrastructure/gantry/pid_controller.h"
#include "infrastructure/gantry/axis_controller_base.h"

/// DC-servo axis: host-side PID over PWM, position from a quadrature encoder.
///
/// Everything to do with the board link, calibration, travel limits and the
/// 50Hz loop lives in AxisControllerBase. What is here is specific to this
/// drive kind: the PID, the PWM ramp limiter, limit-switch homing, and the
/// step-test / relay auto-tune machinery — none of which mean anything for a
/// stepper whose driver closes its own loop.
class GantryAxisController : public AxisControllerBase
{
    Q_OBJECT
public:
    /// Shares an existing board link with the other axes on that board.
    GantryAxisController(AxisBoardLink* link, int axisIndex, QObject* parent = nullptr);

    /// Convenience for a single-axis rig (and for tests): owns a private link.
    explicit GantryAxisController(ISerialTransport* transport = nullptr, QObject* parent = nullptr);

    /// Reply routing from the shared link (IAxisReplyHandler).
    void onReply(const axisproto::Reply& reply) override;

    // PID gains for the position closed loop (PWM output, in encoder-mm error
    // units). Defaults below are a conservative starting point carried over
    // from the old P-only controller's KP; KI/KD are new and need tuning
    // against real hardware settling behavior.
    // ASSUMPTION: verify/tune against real gantry hardware.
    void setPidGains(double kp, double ki, double kd) { m_pid.setGains(kp, ki, kd); }

    // Max PWM change per 20ms control tick — an outer limiter on top of the
    // PID, capping effective acceleration. Too low and the loop can't keep up
    // with a fast trajectory; too high and the axis jerks mechanically.
    void setPwmRampPerTick(int pwmPerTick) { m_pwmRampPerTick = std::clamp(pwmPerTick, 1, MAX_PWM); }
    int  pwmRampPerTick() const { return m_pwmRampPerTick; }

public slots:
    /// Apply the whole closed-loop configuration in one shot. Preferred over
    /// the individual setters when coming from another thread, so the control
    /// loop can't run a tick with new gains against old limits.
    void applyTuning(const GantryTuning& tuning) override;

    // ─── The DC control model (AxisControllerBase) ────────────────────────
    void homeGantry() override;
    void jogGantry(int speedPwm) override;
    void stopJog() override;
    void resetEncoder() override;
    void tick(double targetMm) override;
    void heartbeat() override;

    // ─── PID tuning ───────────────────────────────────────────────────────
    // These deliberately drive the axis. Every one of them is gated on the
    // axis being connected and homed, runs bounded about the middle of
    // travel, and can be stopped at any instant by abortTuning().

    /// Settles at the midpoint of travel, commands a step of `stepSizeUnits`,
    /// captures the response, then returns to the midpoint. Emits
    /// stepTelemetry() every tick while running, then stepTestFinished().
    void startStepTest(double stepSizeUnits);

    /// Astrom-Hagglund relay feedback: settles at the midpoint, then drives
    /// bang-bang at +/- relayAmplitudePwm about it to provoke a limit cycle
    /// whose period and amplitude give Ku and Tu. Emits stepTelemetry() every
    /// tick, then autoTuneFinished(); the caller analyses the trace (see
    /// tuning::analyzeRelayOscillation) rather than the control thread.
    ///
    /// Deliberately oscillates real hardware — same preconditions, bounds and
    /// abort paths as startStepTest().
    void startAutoTune(double relayAmplitudePwm);

    /// Stops any tuning activity immediately: PWM to zero, PID reset, state
    /// back to Idle. Safe to call at any time, including when nothing is
    /// running. Reached by every abort path — user, fault, timeout, limit.
    void abortTuning(const QString& reason);

    bool isTuningActive() const { return m_state == State::StepTest || m_state == State::RelayTune; }

signals:
    /// One per control tick while a tuning run is active (and only then, so
    /// it costs nothing in normal operation). `stale` marks a tick where no
    /// fresh encoder reply arrived, so `measured` is a carried-over value.
    void stepTelemetry(double tSec, double setpoint, double measured, int pwm, bool stale);
    /// The preamble (settling to mid-travel) is over and the measured phase
    /// has begun; the capture clock restarts here. Listeners must discard
    /// anything captured so far, or the settle traverse gets analysed as part
    /// of the response and every metric is computed off the wrong baseline.
    void measurementStarted();
    void stepTestFinished();
    /// Relay run completed cleanly; the captured telemetry is ready to analyse.
    /// Carries back what the relay actually used, so the analysis doesn't have
    /// to assume the caller's requested values were applied verbatim.
    void autoTuneFinished(double relayAmplitudePwm, double centreUnits);
    void tuningAborted(const QString& reason);

protected:
    void onLinkLostImpl() override;
    void resetControlState() override;

private:
    void processClosedLoop();
    void setMotorPwm(int pwm);

    /// One tick of the step-test phase machine. Returns false once the run
    /// has ended (finished or aborted).
    bool serviceStepTest();
    /// One tick of the relay auto-tune phase machine. Same contract.
    bool serviceAutoTune();
    /// Abort checks shared by both tuning runs. Returns false if it aborted.
    bool checkTuningSafety();
    /// The tracking PID without processClosedLoop()'s State::Tracking guard —
    /// the step test owns the loop while it runs.
    void processClosedLoopForTuning();

    enum class State {
        Idle,
        Homing,
        Jogging,
        Tracking,
        StepTest,
        RelayTune
    };
    State m_state = State::Idle;

    // ─── Tuning run state ─────────────────────────────────────────────────
    enum class TunePhase {
        None,
        Settling,   // driving to the midpoint before the step / relay
        Stepping,   // the measured portion of a step test
        Relaying,   // bang-bang limit cycle for auto-tune
        Returning   // easing back to the midpoint afterwards
    };
    TunePhase m_tunePhase = TunePhase::None;

    double m_tuneCentre     = 0.0;  // midpoint of travel, in axis units
    double m_tuneStepSize   = 0.0;
    double m_tuneMargin     = 0.0;  // soft boundary inset from the travel limits
    QElapsedTimer m_tuneElapsed;    // whole-run timeout
    QElapsedTimer m_tuneCapture;    // t=0 at the moment of the step
    int    m_tuneSettleTicks = 0;   // consecutive ticks inside tolerance
    int    m_ticksSinceEncoder = 0; // feedback-loss watchdog
    bool   m_tuneSampleStale = false;

    // Relay auto-tune
    double m_relayAmplitude  = 60.0;  // PWM, the "d" in Ku = 4d/(pi*a)
    double m_relayHysteresis = 0.0;   // axis units; rejects encoder quantisation chatter
    int    m_relayOutput     = 0;     // current bang-bang output
    int    m_tuneTimeoutMs   = 10000; // per-run; auto-tune needs longer than a step

    static constexpr int    RELAY_DURATION_MS      = 15000; // enough for >= 6 cycles
    static constexpr int    AUTOTUNE_TIMEOUT_MS    = 45000;
    static constexpr double RELAY_HYSTERESIS_COUNTS = 2.0;  // encoder counts

    static constexpr int    TUNE_TIMEOUT_MS        = 10000;
    static constexpr int    TUNE_SETTLE_TICKS      = 15;   // ~300ms inside tolerance
    static constexpr double TUNE_SETTLE_TOLERANCE  = 1.0;  // axis units, step test
    // Auto-tune doesn't need a precise start: the relay oscillates about the
    // centre wherever it begins, and the first two cycles are discarded as
    // transient anyway. Insisting on the step test's tolerance just means weak
    // gains can block the very run that would fix them.
    static constexpr double RELAY_SETTLE_FRACTION  = 0.02; // of travel span
    static constexpr double RELAY_SETTLE_FLOOR     = 5.0;  // axis units
    /// Settling is always the first phase, so this is measured from run start.
    /// Far shorter than the run timeout — failing to settle is a distinct
    /// problem from a run that started fine and went wrong later.
    static constexpr int    SETTLE_TIMEOUT_MS      = 8000;

    double m_settleTolerance = TUNE_SETTLE_TOLERANCE; // set per run

    // The settle phase must SLEW its target to the midpoint, not step to it.
    // Commanding the centre outright from far away gives the PID an error of
    // thousands of units: it saturates instantly, drives flat out, overshoots
    // massively and oscillates — a step no controller could follow. Moving the
    // target at a bounded rate keeps the loop in its linear region.
    double m_settleTarget      = 0.0;
    double m_settleSlewPerTick = 0.0;
    /// Seconds to traverse the whole workspace while settling. Sized so a
    /// full-span move still leaves time to settle inside SETTLE_TIMEOUT_MS.
    static constexpr double SETTLE_SLEW_SPAN_SEC = 4.0;
    static constexpr int    TUNE_FEEDBACK_LOSS_TICKS = 10; // 200ms with no encoder
    static constexpr double TUNE_RUNAWAY_FACTOR    = 2.5;  // x excursion from centre
    static constexpr double TUNE_MARGIN_FRACTION   = 0.10; // of total travel span
    static constexpr int    TUNE_STEP_CAPTURE_MS   = 4000;

    // Tracking. m_currentPositionMm and m_travelLimits live in the base —
    // every axis kind has a measured position and a travel range.
    double m_targetPositionMm = 0.0;
    long m_lastEncoderCount = 0;

    // Homing
    int m_homePwm = -100;
    QElapsedTimer m_homingElapsed;
    static constexpr int HOMING_TIMEOUT_MS = 15000;

    // Jogging
    int m_jogPwm = 0;

    // Closed-loop
    int m_currentPwm = 0;
    static constexpr int MAX_PWM = 255;
    // Outer ramp limiter on top of the PID. Configurable via setPwmRampPerTick();
    // the default matches the long-standing hardcoded value.
    int m_pwmRampPerTick = 15;
    gantry::PIDController m_pid{0.8, 0.1, 0.05, -MAX_PWM, MAX_PWM};
    QElapsedTimer m_pidClock;
    bool m_pidClockValid = false;

    // Diagnostic: how many consecutive Tracking ticks the PID has demanded
    // full-effort PWM while still not closing the error — a sign the
    // commanded trajectory (timeline speed/accel) is faster than the gantry
    // can actually track, as opposed to a calibration or serial issue.
    int m_saturatedTicks = 0;
    bool m_saturationLogged = false;
    static constexpr double SATURATION_ERROR_THRESHOLD_MM = 5.0;
    static constexpr int SATURATION_LOG_TICKS = 15; // ~300ms at 20ms/tick

    // v2 replies are self-identifying, so there is no longer a one-outstanding
    // -query rule to enforce — a position query can go out every tick and be
    // matched by its type when it comes back.
    //
    // What must survive from the old PendingQuery machinery is the staleness
    // diagnostic: how long since a real position reading arrived. That log
    // line is what exposed a flaky link during commissioning, and during a
    // tuning run it is the ISR-contention canary.
    int  m_ticksSinceEncoderReply = 0;
    bool m_encoderStaleLogged     = false;
    static constexpr int ENCODER_STALE_TICKS = 10;  // 200ms at 20ms tick rate

    /// Set while awaiting a home-switch answer, so homing only advances on a
    /// real reply rather than on any traffic.
    bool m_awaitingHomeReply = false;
};
