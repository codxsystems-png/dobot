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
#include "infrastructure/gantry/pid_controller.h"
#include "infrastructure/gantry/serial_transport.h"
#include "services/connection_state_machine.h"

class GantryAxisController : public QObject
{
    Q_OBJECT
public:
    // transport == nullptr uses the real QSerialPort-backed transport (or,
    // if Qt6::SerialPort isn't available, a no-op transport that always
    // fails to open). Tests pass a FakeSerialTransport instead — see
    // src/infrastructure/gantry/fake_serial_transport.h.
    explicit GantryAxisController(ISerialTransport* transport = nullptr, QObject* parent = nullptr);
    ~GantryAxisController() override;

    QStringList availablePorts() const;
    bool isConnected() const { return m_connected; }
    bool isHomed() const { return m_isHomed; }
    double currentPositionMm() const { return m_currentPositionMm; }

    void setEncoderCountsPerMm(double countsPerMm);
    double encoderCountsPerMm() const { return m_countsPerMm; }

    // Runtime safety net: tick() clamps any requested target to this range
    // before it ever reaches the closed loop, independent of whatever
    // preflight validation the caller did (or didn't) do upstream.
    void setTravelLimits(const GantryLimits& limits) { m_travelLimits = limits; }
    GantryLimits travelLimits() const { return m_travelLimits; }

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

    ConnectionStateMachine::State connectionState() const { return m_stateMachine->state(); }

public slots:
    /// Apply the whole closed-loop configuration in one shot. Preferred over
    /// the individual setters when coming from another thread, so the control
    /// loop can't run a tick with new gains against old limits.
    void applyTuning(const GantryTuning& tuning);

    bool connectPort(const QString& portName);
    void disconnectPort();

    // Commands
    void homeGantry();
    void jogGantry(int speedPwm);
    void stopJog();
    void resetEncoder();

    // Called periodically by GantryService during playback / holding
    void tick(double targetMm);

    // Fallback heartbeat for when playback tick isn't running (e.g. idle)
    void heartbeat();

    // ─── PID tuning ───────────────────────────────────────────────────────
    // These deliberately drive the axis. Every one of them is gated on the
    // axis being connected and homed, runs bounded about the middle of
    // travel, and can be stopped at any instant by abortTuning().

    /// Settles at the midpoint of travel, commands a step of `stepSizeUnits`,
    /// captures the response, then returns to the midpoint. Emits
    /// stepTelemetry() every tick while running, then stepTestFinished().
    void startStepTest(double stepSizeUnits);

    /// Stops any tuning activity immediately: PWM to zero, PID reset, state
    /// back to Idle. Safe to call at any time, including when nothing is
    /// running. Reached by every abort path — user, fault, timeout, limit.
    void abortTuning(const QString& reason);

    bool isTuningActive() const { return m_state == State::StepTest; }

signals:
    /// One per control tick while a tuning run is active (and only then, so
    /// it costs nothing in normal operation). `stale` marks a tick where no
    /// fresh encoder reply arrived, so `measured` is a carried-over value.
    void stepTelemetry(double tSec, double setpoint, double measured, int pwm, bool stale);
    void stepTestFinished();
    void tuningAborted(const QString& reason);

    void connected(const QString& portName);
    void disconnected();
    void errorOccurred(const QString& message);
    void positionChanged(double positionMm);
    void homed();

private slots:
    void onReadyRead();
    void onTransportError(const QString& message, bool isFatal);
    void onReconnectRequested();

private:
    void sendCommand(const QString& cmd);
    void handleResponse(const QString& response);
    void processClosedLoop();
    void setMotorPwm(int pwm);

    /// One tick of the step-test phase machine. Returns false once the run
    /// has ended (finished or aborted).
    bool serviceStepTest();
    /// The tracking PID without processClosedLoop()'s State::Tracking guard —
    /// the step test owns the loop while it runs.
    void processClosedLoopForTuning();

    // Mechanical teardown only (close port, stop timer, reset flags) — used
    // by disconnectPort() (user-initiated), connectPort()'s own pre-cleanup,
    // and the fault path, each of which then decides separately what (if
    // anything) to tell m_stateMachine.
    void teardownConnection();

    ConnectionStateMachine* m_stateMachine = nullptr;
    QString m_lastPortName;

    ISerialTransport* m_transport = nullptr;
    QTimer* m_controlTimer = nullptr;

    bool m_connected = false;
    bool m_isHomed = false;
    double m_countsPerMm = 100.0;

    enum class State {
        Idle,
        Homing,
        Jogging,
        Tracking,
        StepTest
    };
    State m_state = State::Idle;

    // ─── Tuning run state ─────────────────────────────────────────────────
    enum class TunePhase {
        None,
        Settling,   // driving to the midpoint before the step
        Stepping,   // the measured portion
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

    static constexpr int    TUNE_TIMEOUT_MS        = 10000;
    static constexpr int    TUNE_SETTLE_TICKS      = 15;   // ~300ms inside tolerance
    static constexpr double TUNE_SETTLE_TOLERANCE  = 1.0;  // axis units
    static constexpr int    TUNE_FEEDBACK_LOSS_TICKS = 10; // 200ms with no encoder
    static constexpr double TUNE_RUNAWAY_FACTOR    = 2.5;  // x excursion from centre
    static constexpr double TUNE_MARGIN_FRACTION   = 0.10; // of total travel span
    static constexpr int    TUNE_STEP_CAPTURE_MS   = 4000;

    // Tracking
    double m_currentPositionMm = 0.0;
    double m_targetPositionMm = 0.0;
    long m_lastEncoderCount = 0;
    GantryLimits m_travelLimits;

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

    // Half-duplex request/response tracking for 'e' / 'h' / 'p' queries
    enum class PendingQuery { None, Encoder, Home, Ping };
    PendingQuery m_pendingQuery = PendingQuery::None;
    int m_pendingTicks = 0;
    static constexpr int PENDING_TIMEOUT_TICKS = 10; // 200ms at 20ms tick rate

    QByteArray m_readBuffer;
};
