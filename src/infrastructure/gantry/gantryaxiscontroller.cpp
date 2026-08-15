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

    int delta = std::clamp(desiredPwm - m_currentPwm, -MAX_PWM_CHANGE_PER_TICK, MAX_PWM_CHANGE_PER_TICK);
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
