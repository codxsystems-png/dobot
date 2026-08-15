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

GantryAxisController::GantryAxisController(ISerialTransport* transport, QObject* parent)
    : QObject(parent)
{
    m_transport = transport ? transport : createDefaultSerialTransport(this);
    if (m_transport) {
        m_transport->setParent(this); // GantryAxisController owns whichever transport it ends up with

        connect(m_transport, &ISerialTransport::readyRead, this, &GantryAxisController::onReadyRead);
        connect(m_transport, &ISerialTransport::errorOccurred, this, &GantryAxisController::onTransportError);
    }

    m_stateMachine = new ConnectionStateMachine("GantryAxisController", this);
    m_stateMachine->setBackoffPolicy(1000, 15000, 2.0);
    connect(m_stateMachine, &ConnectionStateMachine::reconnectRequested,
            this, &GantryAxisController::onReconnectRequested);
    connect(m_stateMachine, &ConnectionStateMachine::requiresReHome, this, [this]() {
        emit errorOccurred("Gantry reconnected after a fault — re-homing is required before further motion.");
    });
}

GantryAxisController::~GantryAxisController()
{
    teardownConnection();
}

QStringList GantryAxisController::availablePorts() const
{
    QStringList ports;
#ifdef HAS_SERIALPORT
    for (const auto& info : QSerialPortInfo::availablePorts())
        ports << info.portName();
#endif
    return ports;
}

void GantryAxisController::setEncoderCountsPerMm(double countsPerMm)
{
    m_countsPerMm = qMax(0.1, countsPerMm);
}

void GantryAxisController::applyTuning(const GantryTuning& tuning)
{
    setEncoderCountsPerMm(tuning.countsPerUnit);
    setTravelLimits(tuning.travelLimits);
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

bool GantryAxisController::connectPort(const QString& portName)
{
    m_lastPortName = portName;
    m_stateMachine->notifyConnecting();

    teardownConnection();

    if (!m_transport) {
        QString msg = "Qt6::SerialPort not available — Gantry disabled";
        emit errorOccurred(msg);
        m_stateMachine->notifyFault(msg);
        return false;
    }

    if (!m_transport->open(portName)) {
        QString msg = "Failed to open " + portName + ": " + m_transport->lastErrorString();
        emit errorOccurred(msg);
        m_stateMachine->notifyFault(msg);
        return false;
    }

    m_connected = true;
    m_isHomed = false;
    m_state = State::Idle;
    m_currentPwm = 0;
    m_pendingQuery = PendingQuery::None;
    m_pendingTicks = 0;

    if (!m_controlTimer) {
        m_controlTimer = new QTimer(this);
        connect(m_controlTimer, &QTimer::timeout, this, &GantryAxisController::heartbeat);
    }
    m_controlTimer->start(20); // 50Hz heartbeat for closed-loop control + encoder polling

    // We do not drive the motor here. We wait for the user to explicitly Home the gantry
    // before any motion command is honored (see tick()).

    qDebug() << "GantryAxisController: Connected to" << portName;
    emit connected(portName);
    m_stateMachine->notifyConnected();
    return true;
}

void GantryAxisController::disconnectPort()
{
    bool wasConnected = m_connected;
    teardownConnection();
    if (wasConnected) {
        m_stateMachine->notifyDisconnected("user requested");
    }
}

void GantryAxisController::teardownConnection()
{
    // Losing the link mid-tuning means no feedback and no way to stop the
    // motor over serial — cancel the run before the port closes, while the
    // explicit "g 0" can still get out.
    if (m_state == State::StepTest || m_state == State::RelayTune || m_tunePhase != TunePhase::None) {
        abortTuning("connection closed");
    }

    if (m_transport && m_transport->isOpen()) {
        sendCommand("g 0"); // best-effort stop before closing
        m_transport->flush();
        m_transport->close();
    }

    if (m_controlTimer) {
        m_controlTimer->stop();
    }

    if (m_connected) {
        m_connected = false;
        m_state = State::Idle;
        m_pendingQuery = PendingQuery::None;
        emit disconnected();
        qDebug() << "GantryAxisController: Disconnected";
    }
}

void GantryAxisController::onReconnectRequested()
{
    qDebug() << "GantryAxisController: attempting reconnect to" << m_lastPortName;
    connectPort(m_lastPortName);
}

void GantryAxisController::homeGantry()
{
    if (!m_connected) return;
    qDebug() << "GantryAxisController: Homing (driving toward limit switch)...";

    m_isHomed = false;
    m_state = State::Homing;
    m_pendingQuery = PendingQuery::None;
    m_pendingTicks = 0;
    m_homingElapsed.restart();

    setMotorPwm(m_homePwm);
}

void GantryAxisController::jogGantry(int speedPwm)
{
    if (!m_connected) return;
    m_state = State::Jogging;
    m_jogPwm = std::clamp(speedPwm, -MAX_PWM, MAX_PWM);
    setMotorPwm(m_jogPwm);
}

void GantryAxisController::stopJog()
{
    if (!m_connected) return;
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
    if (!m_connected) return;
    sendCommand("r");
    m_lastEncoderCount = 0;
    m_currentPositionMm = 0.0;
    emit positionChanged(m_currentPositionMm);
}

void GantryAxisController::tick(double targetMm)
{
    if (!m_connected || !m_isHomed) return;

    // A tuning run owns the control loop. The UI already refuses to start one
    // while playback is active, but guard here too — two sources commanding
    // the same axis would corrupt the measurement and could fight mid-motion.
    if (m_state == State::StepTest || m_state == State::RelayTune) return;

    // Runtime safety net — clamp before the target ever reaches the closed
    // loop, regardless of whether the caller already validated it upstream.
    double clampedMm = std::clamp(targetMm, m_travelLimits.minMm, m_travelLimits.maxMm);
    if (clampedMm != targetMm) {
        QString msg = QString("Gantry target %1mm clamped to travel limit %2mm")
                          .arg(targetMm, 0, 'f', 1).arg(clampedMm, 0, 'f', 1);
        StructuredLogger::instance().log(StructuredLogger::Category::Safety,
            "GantryAxisController", msg);
        emit errorOccurred(msg);
    }

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
    if (!m_connected) {
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
    if (!m_connected) {
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
        if (m_connected) setMotorPwm(0);
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
    if (!m_connected) return;

    // A query ('e' or 'h') expects exactly one reply; if it never arrives
    // (dropped byte, noise) don't let the half-duplex link jam forever.
    if (m_pendingQuery != PendingQuery::None) {
        if (++m_pendingTicks > PENDING_TIMEOUT_TICKS) {
            // Previously silent — an encoder query timing out during Tracking
            // means the PID closed loop just ran (or is about to run) another
            // tick against a stale m_currentPositionMm, up to PENDING_TIMEOUT_TICKS
            // ticks old. Logged so intermittent serial drops during playback
            // are diagnosable instead of just showing up as "inaccurate".
            if (m_pendingQuery == PendingQuery::Encoder) {
                StructuredLogger::instance().log(StructuredLogger::Category::Motion,
                    "GantryAxisController",
                    QString("Encoder query timed out (no response within %1ms) while in state %2 — "
                            "position feedback is stale.")
                        .arg(PENDING_TIMEOUT_TICKS * 20)
                        .arg(m_state == State::Tracking ? "Tracking" :
                             m_state == State::Jogging  ? "Jogging"  :
                             m_state == State::Homing    ? "Homing"   : "Idle"));
            }
            m_pendingQuery = PendingQuery::None;
            m_pendingTicks = 0;
        }
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
        // Must keep resending, or the Arduino's own link/motor watchdog (500ms)
        // zeroes the PWM mid-drive since 'h' polls don't count as motor commands.
        setMotorPwm(m_homePwm);
        if (m_pendingQuery == PendingQuery::None) {
            sendCommand("h");
            m_pendingQuery = PendingQuery::Home;
            m_pendingTicks = 0;
        }
        break;

    case State::Jogging:
        // Resend so the Arduino's link-loss watchdog sees a live host.
        setMotorPwm(m_jogPwm);
        if (m_pendingQuery == PendingQuery::None) {
            sendCommand("e");
            m_pendingQuery = PendingQuery::Encoder;
            m_pendingTicks = 0;
        }
        break;

    case State::StepTest:
    case State::RelayTune: {
        // Feedback-loss watchdog: reset in handleResponse() on a real reading.
        ++m_ticksSinceEncoder;
        m_tuneSampleStale = (m_ticksSinceEncoder > 1);
        bool stillRunning = (m_state == State::StepTest) ? serviceStepTest()
                                                          : serviceAutoTune();
        if (!stillRunning) return; // run ended (finished or aborted)
        if (m_pendingQuery == PendingQuery::None) {
            sendCommand("e");
            m_pendingQuery = PendingQuery::Encoder;
            m_pendingTicks = 0;
        }
        break;
    }

    case State::Idle:
    case State::Tracking:
        if (m_pendingQuery == PendingQuery::None) {
            sendCommand("e");
            m_pendingQuery = PendingQuery::Encoder;
            m_pendingTicks = 0;
        }
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
    sendCommand("g " + QString::number(m_currentPwm));
}

void GantryAxisController::sendCommand(const QString& cmd)
{
    if (!m_transport || !m_transport->isOpen()) return;
    m_transport->write(cmd.toUtf8() + "\n");
}

void GantryAxisController::onReadyRead()
{
    m_readBuffer.append(m_transport->readAll());

    while (m_readBuffer.contains('\n')) {
        int idx = m_readBuffer.indexOf('\n');
        QString line = QString::fromUtf8(m_readBuffer.left(idx)).trimmed();
        m_readBuffer.remove(0, idx + 1);

        if (!line.isEmpty()) {
            handleResponse(line);
        }
    }
}

void GantryAxisController::handleResponse(const QString& response)
{
    PendingQuery query = m_pendingQuery;
    m_pendingQuery = PendingQuery::None;
    m_pendingTicks = 0;

    switch (query) {
    case PendingQuery::Encoder: {
        bool ok = false;
        long counts = response.toLong(&ok);
        if (ok) {
            m_lastEncoderCount = counts;
            m_currentPositionMm = counts / m_countsPerMm;
            m_ticksSinceEncoder = 0; // fresh reading — feedback watchdog satisfied
            emit positionChanged(m_currentPositionMm);
        }
        break;
    }
    case PendingQuery::Home: {
        if (response.trimmed() == "1") {
            setMotorPwm(0);
            sendCommand("r");
            m_lastEncoderCount = 0;
            m_currentPositionMm = 0.0;
            m_targetPositionMm = 0.0;
            m_isHomed = true;
            m_state = State::Idle;
            emit positionChanged(0.0);
            emit homed();
            qDebug() << "GantryAxisController: Homed.";
        }
        // else: switch not reached yet — heartbeat() will keep polling while Homing.
        break;
    }
    case PendingQuery::Ping:
    case PendingQuery::None:
        break;
    }
}

void GantryAxisController::onTransportError(const QString& message, bool isFatal)
{
    QString msg = "Serial error: " + message;
    StructuredLogger::instance().log(StructuredLogger::Category::Connection,
        "GantryAxisController", msg);
    emit errorOccurred(msg);
    if (isFatal) {
        // Unexpected loss of the device (unplugged, driver reset, ...) — this
        // is a fault, not a user-requested disconnect, so it schedules an
        // auto-reconnect (with backoff) instead of just going quiet.
        teardownConnection();
        m_stateMachine->notifyFault(msg);
    }
}
