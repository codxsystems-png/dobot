// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Gantry Axis Controller
// ═══════════════════════════════════════════════════════════════════════════════

#include "infrastructure/gantry/gantryaxiscontroller.h"
#include "infrastructure/gantry/qt_serial_transport.h"
#include "core/structured_logger.h"
#include <QDebug>
#include <cmath>
#include <algorithm>

#ifdef HAS_SERIALPORT
#include <QSerialPortInfo>
#endif

GantryAxisController::GantryAxisController(AxisBoardLink* link, int axisIndex, QObject* parent)
    : AxisControllerBase(link, axisIndex, parent)
{
}

GantryAxisController::GantryAxisController(ISerialTransport* transport, QObject* parent)
    : AxisControllerBase(transport, 0, parent)   // DC servo is axis 0
{
}

void GantryAxisController::resetControlState()
{
    m_state      = State::Idle;
    m_currentPwm = 0;
    m_ticksSinceEncoderReply = 0;
    m_encoderStaleLogged     = false;
    m_awaitingHomeReply      = false;
}

void GantryAxisController::onLinkLostImpl()
{
    // Losing the link mid-tuning means no feedback and no way to stop the
    // motor over serial — cancel the run while an explicit stop can still
    // get out. The base tears the loop down after this returns.
    if (m_state == State::StepTest || m_state == State::RelayTune || m_tunePhase != TunePhase::None) {
        abortTuning("connection closed");
    }

    resetControlState();
}

void GantryAxisController::applyTuning(const GantryTuning& tuning)
{
    AxisControllerBase::applyTuning(tuning);   // calibration + travel limits
    setPidGains(tuning.pidKp, tuning.pidKi, tuning.pidKd);
    setPwmRampPerTick(tuning.pwmRampPerTick);

    // Gains changing mid-flight leave an integral accumulated under the old
    // ones, which is meaningless — and a scale-factor change makes every
    // historical error term wrong outright.
    m_pid.reset();
    m_pidClockValid = false;

    StructuredLogger::instance().log(StructuredLogger::Category::Motion,
        "GantryAxisController",
        QString("Tuning applied: %1 counts/unit, travel [%2, %3], ramp %4 PWM/tick, "
                "PID(%5, %6, %7)")
            .arg(tuning.countsPerUnit, 0, 'f', 3)
            .arg(tuning.travelLimits.minMm, 0, 'f', 1)
            .arg(tuning.travelLimits.maxMm, 0, 'f', 1)
            .arg(tuning.pwmRampPerTick)
            .arg(tuning.pidKp, 0, 'f', 4)
            .arg(tuning.pidKi, 0, 'f', 4)
            .arg(tuning.pidKd, 0, 'f', 4));
}

void GantryAxisController::homeGantry()
{
    if (!isIdentified()) return;
    qDebug() << "GantryAxisController: Homing (driving toward limit switch)...";

    m_isHomed = false;
    m_state = State::Homing;
    m_awaitingHomeReply = false;
    m_homingElapsed.restart();

    setMotorPwm(m_homePwm);
}

void GantryAxisController::jogGantry(int speedPwm)
{
    if (!isIdentified()) return;
    m_state = State::Jogging;
    m_jogPwm = std::clamp(speedPwm, -MAX_PWM, MAX_PWM);
    setMotorPwm(m_jogPwm);
}

void GantryAxisController::stopJog()
{
    if (!isIdentified()) return;
    // Also used as the general stop path for Pause/E-STOP, so unconditionally
    // halt the motor and cancel homing/tracking regardless of current state.
    if (m_state == State::StepTest || m_state == State::RelayTune || m_tunePhase != TunePhase::None) {
        abortTuning("stopped");
        return; // abortTuning already zeroed the motor and reset state
    }
    m_jogPwm = 0;
    m_state = State::Idle;
    setMotorPwm(0);
}

void GantryAxisController::resetEncoder()
{
    if (!isIdentified()) return;
    if (m_link) m_link->send(axisproto::cmdZero(m_axisIndex));
    m_lastEncoderCount = 0;
    m_currentPositionMm = 0.0;
    emit positionChanged(m_currentPositionMm);
}

void GantryAxisController::tick(double targetMm)
{
    if (!isIdentified() || !m_isHomed) return;

    // A tuning run owns the control loop. The UI already refuses to start one
    // while playback is active, but guard here too — two sources commanding
    // the same axis would corrupt the measurement and could fight mid-motion.
    if (m_state == State::StepTest || m_state == State::RelayTune) return;

    // Runtime safety net — clamp before the target ever reaches the closed
    // loop, regardless of whether the caller already validated it upstream.
    double clampedMm = clampToTravel(targetMm);

    if (m_state != State::Homing && m_state != State::Jogging) {
        if (m_state != State::Tracking) {
            // Freshly (re-)entering tracking — clear PID state and the dt
            // clock so a stale integral or a huge derivative spike from
            // however long we were idle/homing doesn't throw the first output.
            m_pid.reset();
            m_pidClockValid = false;
        }
        m_state = State::Tracking;
        m_targetPositionMm = clampedMm;
        processClosedLoop();
    }
}

void GantryAxisController::startStepTest(double stepSizeUnits)
{
    // Preconditions are also enforced in the UI (buttons stay disabled), but
    // re-checked here so nothing can drive the axis by calling this directly.
    if (!isIdentified()) {
        emit errorOccurred("Step test needs the axis connected.");
        return;
    }
    if (!m_isHomed) {
        emit errorOccurred("Step test needs the axis homed — without a known "
                           "origin, position and travel limits are meaningless.");
        return;
    }

    double span = m_travelLimits.maxMm - m_travelLimits.minMm;
    double excursion = std::abs(stepSizeUnits);
    if (span <= 0.0 || excursion <= 0.0 || span < 4.0 * excursion) {
        emit errorOccurred(QString("Step of %1 is too large for the configured travel "
                                    "range of %2 — needs at least 4x headroom.")
                               .arg(excursion, 0, 'f', 1).arg(span, 0, 'f', 1));
        return;
    }

    m_tuneCentre   = (m_travelLimits.minMm + m_travelLimits.maxMm) / 2.0;
    m_tuneStepSize = stepSizeUnits;
    m_tuneMargin   = span * TUNE_MARGIN_FRACTION;
    m_tuneTimeoutMs = TUNE_TIMEOUT_MS;
    // A step test needs a genuinely stable baseline to measure against.
    m_settleTolerance = TUNE_SETTLE_TOLERANCE;
    m_settleTarget      = m_currentPositionMm;
    m_settleSlewPerTick = (span / SETTLE_SLEW_SPAN_SEC) * 0.020;
    m_tuneSettleTicks   = 0;
    m_ticksSinceEncoder = 0;
    m_tunePhase = TunePhase::Settling;
    m_state     = State::StepTest;

    m_pid.reset();
    m_pidClockValid = false;
    m_targetPositionMm = m_tuneCentre;
    m_tuneElapsed.restart();
    m_tuneCapture.restart();

    StructuredLogger::instance().log(StructuredLogger::Category::Motion,
        "GantryAxisController",
        QString("Step test starting: centre %1, step %2, margin %3")
            .arg(m_tuneCentre, 0, 'f', 1)
            .arg(m_tuneStepSize, 0, 'f', 1)
            .arg(m_tuneMargin, 0, 'f', 1));
}

void GantryAxisController::startAutoTune(double relayAmplitudePwm)
{
    if (!isIdentified()) {
        emit errorOccurred("Auto-tune needs the axis connected.");
        return;
    }
    if (!m_isHomed) {
        emit errorOccurred("Auto-tune needs the axis homed — without a known "
                           "origin, position and travel limits are meaningless.");
        return;
    }

    double span = m_travelLimits.maxMm - m_travelLimits.minMm;
    if (span <= 0.0) {
        emit errorOccurred("Auto-tune needs a configured travel range.");
        return;
    }

    m_relayAmplitude = std::clamp(std::abs(relayAmplitudePwm), 1.0,
                                   static_cast<double>(MAX_PWM));
    // Two encoder counts' worth: enough to reject quantisation chatter at the
    // switching point without smothering a genuinely small limit cycle.
    m_relayHysteresis = RELAY_HYSTERESIS_COUNTS / m_countsPerMm;
    m_relayOutput = 0;

    m_tuneCentre = (m_travelLimits.minMm + m_travelLimits.maxMm) / 2.0;
    m_tuneMargin = span * TUNE_MARGIN_FRACTION;
    // The relay has no commanded excursion of its own, so bound the runaway
    // check against a quarter of the usable span.
    m_tuneStepSize = span * 0.25;
    m_tuneTimeoutMs = AUTOTUNE_TIMEOUT_MS;
    m_settleTolerance = std::max(RELAY_SETTLE_FLOOR, span * RELAY_SETTLE_FRACTION);
    m_settleTarget      = m_currentPositionMm;
    m_settleSlewPerTick = (span / SETTLE_SLEW_SPAN_SEC) * 0.020;

    m_tuneSettleTicks   = 0;
    m_ticksSinceEncoder = 0;
    m_tunePhase = TunePhase::Settling;
    m_state     = State::RelayTune;

    m_pid.reset();
    m_pidClockValid = false;
    m_targetPositionMm = m_tuneCentre;
    m_tuneElapsed.restart();
    m_tuneCapture.restart();

    StructuredLogger::instance().log(StructuredLogger::Category::Motion,
        "GantryAxisController",
        QString("Auto-tune starting: centre %1, relay +/-%2 PWM, hysteresis %3")
            .arg(m_tuneCentre, 0, 'f', 1)
            .arg(m_relayAmplitude, 0, 'f', 0)
            .arg(m_relayHysteresis, 0, 'f', 4));
}

// One tick of the relay auto-tune. Settles at the centre under PID, then
// switches to open-loop bang-bang to provoke a limit cycle.
bool GantryAxisController::serviceAutoTune()
{
    if (!checkTuningSafety()) return false;

    switch (m_tunePhase) {
    case TunePhase::Settling:
        // Advance the commanded target toward the centre at a bounded rate
        // rather than jumping to it, so the PID never sees a huge error.
        if (m_settleTarget < m_tuneCentre) {
            m_settleTarget = std::min(m_tuneCentre, m_settleTarget + m_settleSlewPerTick);
        } else {
            m_settleTarget = std::max(m_tuneCentre, m_settleTarget - m_settleSlewPerTick);
        }
        m_targetPositionMm = m_settleTarget;
        if (std::abs(m_currentPositionMm - m_tuneCentre) < m_settleTolerance) {
            if (++m_tuneSettleTicks >= TUNE_SETTLE_TICKS) {
                m_tunePhase = TunePhase::Relaying;
                m_tuneCapture.restart();  // t = 0 at the start of the limit cycle
                emit measurementStarted(); // listeners drop the settle preamble
            }
        } else {
            m_tuneSettleTicks = 0;
        }
        processClosedLoopForTuning();
        break;

    case TunePhase::Relaying: {
        if (m_tuneCapture.elapsed() > RELAY_DURATION_MS) {
            m_tunePhase = TunePhase::Returning;
            m_pid.reset();          // PID takes over again for the return
            m_pidClockValid = false;
            m_targetPositionMm = m_tuneCentre;
            break;
        }
        // Bang-bang with hysteresis. Deliberately NOT through the PID or the
        // ramp limiter — the describing-function result Ku = 4d/(pi*a)
        // assumes an ideal relay, so any smoothing here would corrupt it.
        double error = m_tuneCentre - m_currentPositionMm;
        if (error > m_relayHysteresis) {
            m_relayOutput = static_cast<int>(m_relayAmplitude);
        } else if (error < -m_relayHysteresis) {
            m_relayOutput = -static_cast<int>(m_relayAmplitude);
        }
        // Re-sent every tick so the firmware's 500ms watchdog never trips
        // mid-oscillation and silently corrupts the measurement.
        setMotorPwm(m_relayOutput);
        m_targetPositionMm = m_tuneCentre;
        break;
    }

    case TunePhase::Returning:
        m_targetPositionMm = m_tuneCentre;
        if (std::abs(m_currentPositionMm - m_tuneCentre) < m_settleTolerance) {
            setMotorPwm(0);
            double amplitude = m_relayAmplitude;
            double centre    = m_tuneCentre;
            m_tunePhase = TunePhase::None;
            m_state     = State::Idle;
            StructuredLogger::instance().log(StructuredLogger::Category::Motion,
                "GantryAxisController", "Auto-tune relay run finished");
            emit autoTuneFinished(amplitude, centre);
            return false;
        }
        processClosedLoopForTuning();
        break;

    case TunePhase::Stepping:
    case TunePhase::None:
        return false;
    }

    emit stepTelemetry(m_tuneCapture.elapsed() / 1000.0,
                       m_tuneCentre,
                       m_currentPositionMm,
                       m_currentPwm,
                       m_tuneSampleStale);
    return true;
}

void GantryAxisController::abortTuning(const QString& reason)
{
    if (m_state != State::StepTest && m_state != State::RelayTune && m_tunePhase == TunePhase::None) {
        // Nothing running — still force PWM low, since this is the panic path
        // and being certain costs one serial write.
        if (isIdentified()) setMotorPwm(0);
        return;
    }

    setMotorPwm(0);          // explicit stop first, never lean on the firmware watchdog
    m_pid.reset();
    m_pidClockValid = false;
    m_tunePhase = TunePhase::None;
    m_state     = State::Idle;

    StructuredLogger::instance().log(StructuredLogger::Category::Safety,
        "GantryAxisController", "Tuning aborted: " + reason);
    emit tuningAborted(reason);
}

// Hard aborts shared by both tuning runs, checked before anything drives the
// motor. Returns false if it aborted.
bool GantryAxisController::checkTuningSafety()
{
    if (m_tuneElapsed.elapsed() > m_tuneTimeoutMs) {
        abortTuning("timed out");
        return false;
    }
    // The margin and runaway checks exist to catch a run ESCAPING toward the
    // limits — not to stop one travelling in. During Settling the axis is
    // deliberately crossing the workspace to reach the midpoint, and it
    // legitimately starts outside the safe band: homing parks at the travel
    // minimum, so every run would otherwise abort before it began. Settling
    // is still bounded by its own 8s deadline, the feedback-loss check, and
    // a commanded target that is inside the limits by construction.
    if (m_tunePhase != TunePhase::Settling) {
        if (m_currentPositionMm < m_travelLimits.minMm + m_tuneMargin ||
            m_currentPositionMm > m_travelLimits.maxMm - m_tuneMargin) {
            abortTuning(QString("reached the travel-limit margin at %1")
                            .arg(m_currentPositionMm, 0, 'f', 1));
            return false;
        }
        if (std::abs(m_currentPositionMm - m_tuneCentre)
                > TUNE_RUNAWAY_FACTOR * std::abs(m_tuneStepSize)) {
            abortTuning(QString("runaway — %1 is far beyond the commanded excursion")
                            .arg(m_currentPositionMm, 0, 'f', 1));
            return false;
        }
    }
    if (m_ticksSinceEncoder > TUNE_FEEDBACK_LOSS_TICKS) {
        abortTuning("lost encoder feedback — the loop would be flying blind");
        return false;
    }

    // Failing to reach the midpoint is a distinct problem from a run that
    // started fine and went wrong later, so it gets its own (much shorter)
    // deadline and an explanation that names the usual cause. Settling is
    // always the first phase, so run-elapsed is the right clock here.
    if (m_tunePhase == TunePhase::Settling && m_tuneElapsed.elapsed() > SETTLE_TIMEOUT_MS) {
        abortTuning(QString("could not settle at the midpoint (%1) within %2s — stopped "
                            "%3 away. The current gains are probably too weak to overcome "
                            "friction; try raising Kp.")
                        .arg(m_tuneCentre, 0, 'f', 1)
                        .arg(SETTLE_TIMEOUT_MS / 1000)
                        .arg(std::abs(m_currentPositionMm - m_tuneCentre), 0, 'f', 1));
        return false;
    }
    return true;
}

// Runs once per control tick while a step test is active. Returns false if
// the run ended (finished or aborted) so the caller can stop driving it.
bool GantryAxisController::serviceStepTest()
{
    if (!checkTuningSafety()) return false;

    // ─── Phase machine ────────────────────────────────────────────────────
    switch (m_tunePhase) {
    case TunePhase::Settling:
        // Advance the commanded target toward the centre at a bounded rate
        // rather than jumping to it, so the PID never sees a huge error.
        if (m_settleTarget < m_tuneCentre) {
            m_settleTarget = std::min(m_tuneCentre, m_settleTarget + m_settleSlewPerTick);
        } else {
            m_settleTarget = std::max(m_tuneCentre, m_settleTarget - m_settleSlewPerTick);
        }
        m_targetPositionMm = m_settleTarget;
        if (std::abs(m_currentPositionMm - m_tuneCentre) < m_settleTolerance) {
            if (++m_tuneSettleTicks >= TUNE_SETTLE_TICKS) {
                m_tunePhase = TunePhase::Stepping;
                m_targetPositionMm = m_tuneCentre + m_tuneStepSize;
                m_tuneCapture.restart();   // t = 0 at the step
                emit measurementStarted(); // listeners drop the settle preamble
            }
        } else {
            m_tuneSettleTicks = 0;
        }
        break;

    case TunePhase::Stepping:
        m_targetPositionMm = m_tuneCentre + m_tuneStepSize;
        if (m_tuneCapture.elapsed() > TUNE_STEP_CAPTURE_MS) {
            m_tunePhase = TunePhase::Returning;
            m_targetPositionMm = m_tuneCentre;
        }
        break;

    case TunePhase::Returning:
        m_targetPositionMm = m_tuneCentre;
        if (std::abs(m_currentPositionMm - m_tuneCentre) < m_settleTolerance) {
            setMotorPwm(0);
            m_tunePhase = TunePhase::None;
            m_state     = State::Idle;
            StructuredLogger::instance().log(StructuredLogger::Category::Motion,
                "GantryAxisController", "Step test finished");
            emit stepTestFinished();
            return false;
        }
        break;

    case TunePhase::Relaying:
    case TunePhase::None:
        return false;
    }

    // Drive the loop, then report this tick. Telemetry is emitted every tick
    // regardless of whether a fresh encoder value arrived, with `stale` set —
    // a gap in the trace is more honest than a silently interpolated one.
    processClosedLoopForTuning();
    emit stepTelemetry(m_tuneCapture.elapsed() / 1000.0,
                       m_targetPositionMm,
                       m_currentPositionMm,
                       m_currentPwm,
                       m_tuneSampleStale);
    return true;
}

// The tracking PID, but without the State::Tracking guard in
// processClosedLoop() — the step test owns the loop while it runs.
void GantryAxisController::processClosedLoopForTuning()
{
    double dtSec = 0.0;
    if (m_pidClockValid) dtSec = m_pidClock.nsecsElapsed() / 1e9;
    m_pidClock.start();
    m_pidClockValid = true;

    double error = m_targetPositionMm - m_currentPositionMm;
    double desiredPwmD = m_pid.compute(error, m_currentPositionMm, dtSec);
    int desiredPwm = std::clamp(static_cast<int>(std::round(desiredPwmD)), -MAX_PWM, MAX_PWM);

    int delta = std::clamp(desiredPwm - m_currentPwm, -m_pwmRampPerTick, m_pwmRampPerTick);
    setMotorPwm(m_currentPwm + delta);
}

void GantryAxisController::heartbeat()
{
    if (!isIdentified()) return;

    // Staleness watch. Replies are self-identifying now, so nothing has to be
    // held back waiting for an answer — but if answers stop arriving, the PID
    // is running against an increasingly old position and the operator needs
    // to know. This log line is what exposed a flaky link during
    // commissioning, and during a tuning run it is the ISR-contention canary.
    if (++m_ticksSinceEncoderReply > ENCODER_STALE_TICKS && !m_encoderStaleLogged) {
        m_encoderStaleLogged = true;
        StructuredLogger::instance().log(StructuredLogger::Category::Motion,
            "GantryAxisController",
            QString("Encoder query timed out (no response within %1ms) while in state %2 — "
                    "position feedback is stale.")
                .arg(ENCODER_STALE_TICKS * 20)
                .arg(m_state == State::Tracking ? "Tracking" :
                     m_state == State::Jogging  ? "Jogging"  :
                     m_state == State::Homing    ? "Homing"   : "Idle"));
    }

    switch (m_state) {
    case State::Homing:
        if (m_homingElapsed.elapsed() > HOMING_TIMEOUT_MS) {
            setMotorPwm(0);
            m_state = State::Idle;
            StructuredLogger::instance().log(StructuredLogger::Category::Safety,
                "GantryAxisController", "Homing timed out — home switch not reached.");
            emit errorOccurred("Gantry homing timed out — home switch not reached.");
            return;
        }
        // Must keep resending, or the Arduino's own motor watchdog (500ms)
        // zeroes the PWM mid-drive — only a drive command refreshes it.
        setMotorPwm(m_homePwm);
        if (!m_awaitingHomeReply && m_link) {
            m_link->send(axisproto::cmdHome(m_axisIndex));
            m_awaitingHomeReply = true;
        }
        break;

    case State::Jogging:
        // Resend so the Arduino's link-loss watchdog sees a live host.
        setMotorPwm(m_jogPwm);
        if (m_link) m_link->send(axisproto::cmdQuery(m_axisIndex));
        break;

    case State::StepTest:
    case State::RelayTune: {
        // Feedback-loss watchdog: reset in handleResponse() on a real reading.
        ++m_ticksSinceEncoder;
        m_tuneSampleStale = (m_ticksSinceEncoder > 1);
        bool stillRunning = (m_state == State::StepTest) ? serviceStepTest()
                                                          : serviceAutoTune();
        if (!stillRunning) return; // run ended (finished or aborted)
        if (m_link) m_link->send(axisproto::cmdQuery(m_axisIndex));
        break;
    }

    case State::Idle:
    case State::Tracking:
        if (m_link) m_link->send(axisproto::cmdQuery(m_axisIndex));
        break;
    }
}

void GantryAxisController::processClosedLoop()
{
    if (m_state != State::Tracking) return;

    // PID control on measured encoder position (m_currentPositionMm,
    // refreshed by 'e' polling in heartbeat()) toward the timeline's target,
    // with a ramp limit so the PWM can't jump and cause a mechanical jerk.
    double dtSec = 0.0;
    if (m_pidClockValid) {
        dtSec = m_pidClock.nsecsElapsed() / 1e9;
    }
    m_pidClock.start();
    m_pidClockValid = true;

    double error = m_targetPositionMm - m_currentPositionMm;
    double desiredPwmD = m_pid.compute(error, m_currentPositionMm, dtSec);
    int desiredPwm = std::clamp(static_cast<int>(std::round(desiredPwmD)), -MAX_PWM, MAX_PWM);

    // Diagnostic: sustained full-effort PWM with a large uncorrected error
    // means the commanded trajectory is outrunning what the motor can
    // actually deliver — surfaced once per episode rather than every tick.
    bool saturated = std::abs(desiredPwm) >= MAX_PWM && std::abs(error) > SATURATION_ERROR_THRESHOLD_MM;
    if (saturated) {
        if (++m_saturatedTicks >= SATURATION_LOG_TICKS && !m_saturationLogged) {
            m_saturationLogged = true;
            StructuredLogger::instance().log(StructuredLogger::Category::Motion,
                "GantryAxisController",
                QString("PID saturated at max PWM for >%1ms with %2mm uncorrected error — "
                        "commanded trajectory may be faster than the gantry can track. "
                        "Target=%3mm, Actual=%4mm.")
                    .arg(SATURATION_LOG_TICKS * 20)
                    .arg(std::abs(error), 0, 'f', 1)
                    .arg(m_targetPositionMm, 0, 'f', 1)
                    .arg(m_currentPositionMm, 0, 'f', 1));
        }
    } else {
        m_saturatedTicks = 0;
        m_saturationLogged = false;
    }

    int delta = std::clamp(desiredPwm - m_currentPwm, -m_pwmRampPerTick, m_pwmRampPerTick);
    setMotorPwm(m_currentPwm + delta);
}

void GantryAxisController::setMotorPwm(int pwm)
{
    m_currentPwm = std::clamp(pwm, -MAX_PWM, MAX_PWM);
    if (m_link) m_link->send(axisproto::cmdPwm(m_axisIndex, m_currentPwm));
}

void GantryAxisController::onReply(const axisproto::Reply& reply)
{
    switch (reply.type) {

    case 'Q': {   // position, in raw encoder counts
        if (reply.args.isEmpty()) break;
        bool ok = false;
        long counts = reply.args.at(0).toLong(&ok);
        if (!ok) break;

        m_lastEncoderCount  = counts;
        m_currentPositionMm = counts / m_countsPerMm;

        // Fresh reading — both the tuning feedback watchdog and the staleness
        // diagnostic are satisfied.
        m_ticksSinceEncoder      = 0;
        m_ticksSinceEncoderReply = 0;
        m_encoderStaleLogged     = false;

        emit positionChanged(m_currentPositionMm);
        break;
    }

    case 'H': {   // home switch state
        m_awaitingHomeReply = false;
        if (m_state != State::Homing) break;
        if (reply.args.isEmpty() || reply.args.at(0) != "1") break;

        setMotorPwm(0);
        if (m_link) m_link->send(axisproto::cmdZero(m_axisIndex));
        m_lastEncoderCount  = 0;
        m_currentPositionMm = 0.0;
        m_targetPositionMm  = 0.0;
        m_isHomed = true;
        m_state   = State::Idle;
        emit positionChanged(0.0);
        emit homed();
        qDebug() << "GantryAxisController: Homed.";
        break;
    }

    case '!': {   // asynchronous fault — arrives whenever the board decides
        const int code = reply.args.isEmpty() ? 0 : reply.args.at(0).toInt();
        const QString text = reply.args.size() > 1 ? reply.args.at(1) : QString("fault");

        QString msg = QString("Axis board fault %1: %2").arg(code).arg(text);
        StructuredLogger::instance().log(StructuredLogger::Category::Safety,
            "GantryAxisController", msg);
        emit errorOccurred(msg);

        // A halted board isn't going to honour anything further, so stop any
        // tuning run rather than letting it time out confusingly.
        if (m_state == State::StepTest || m_state == State::RelayTune) {
            abortTuning(text);
        }
        break;
    }

    case 'S':   // status — polled for diagnostics; nothing acts on it yet
    case 'A':
    default:
        break;
    }
}
