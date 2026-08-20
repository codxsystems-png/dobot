// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Stepper Axis Controller
// ═══════════════════════════════════════════════════════════════════════════════

#include "infrastructure/gantry/stepper_axis_controller.h"
#include "core/structured_logger.h"
#include <QDebug>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

StepperAxisController::StepperAxisController(AxisBoardLink* link, int axisIndex, QObject* parent)
    : AxisControllerBase(link, axisIndex, parent)
{
}

StepperAxisController::StepperAxisController(ISerialTransport* transport, QObject* parent)
    : AxisControllerBase(transport, 1, parent)   // stepper is axis 1 on the Uno
{
}

long StepperAxisController::unitsToSteps(double units) const
{
    return std::lround(units * m_countsPerMm);
}

double StepperAxisController::stepsToUnits(long steps) const
{
    return static_cast<double>(steps) / m_countsPerMm;
}

void StepperAxisController::setStepRateLimits(long vmaxStepsPerSec, long amaxStepsPerSec2)
{
    m_vmax = qMax(1L, vmaxStepsPerSec);
    m_amax = qMax(1L, amaxStepsPerSec2);
    sendLimits();
}

void StepperAxisController::sendLimits()
{
    if (m_link && isIdentified()) {
        m_link->send(axisproto::cmdLimits(m_axisIndex, m_vmax, m_amax));
    }
}

void StepperAxisController::applyTuning(const GantryTuning& tuning)
{
    AxisControllerBase::applyTuning(tuning);   // calibration + travel limits
    sendLimits();

    StructuredLogger::instance().log(StructuredLogger::Category::Motion,
        "StepperAxisController",
        QString("Tuning applied: %1 steps/unit, travel [%2, %3], vmax %4 steps/s, amax %5 steps/s2")
            .arg(tuning.countsPerUnit, 0, 'f', 3)
            .arg(tuning.travelLimits.minMm, 0, 'f', 1)
            .arg(tuning.travelLimits.maxMm, 0, 'f', 1)
            .arg(m_vmax)
            .arg(m_amax));
}

void StepperAxisController::setEnabled(bool on)
{
    if (!isIdentified()) return;
    m_enabled = on;
    if (m_link) m_link->send(axisproto::cmdEnable(m_axisIndex, on));

    if (!on) {
        // Torque is gone, so nothing is holding position any more. Whatever
        // the axis does next — settle, sag, get nudged — the step count no
        // longer describes where it is.
        m_isHomed = false;
        m_state   = State::Idle;
        StructuredLogger::instance().log(StructuredLogger::Category::Safety,
            "StepperAxisController",
            "Axis released (ENABLE de-asserted) — position reference discarded.");
    }
}

void StepperAxisController::clearFault()
{
    if (!isIdentified()) return;
    if (m_link) m_link->send(axisproto::cmdResume(m_axisIndex));

    m_halted        = false;
    m_stalledPolls  = 0;
    m_stallReported = false;
    m_state         = State::Idle;

    // The board adopts its own current position as the target on R, so the
    // host must follow it rather than keep commanding the pre-fault target.
    m_targetSteps = m_boardSteps;
}

void StepperAxisController::homeGantry()
{
    if (!isIdentified()) {
        emit errorOccurred("Cannot set zero — the axis board is not connected.");
        return;
    }
    if (m_alarmed) {
        emit errorOccurred("Cannot set zero while the drive alarm is asserted. "
                           "Clear the fault at the drive first.");
        return;
    }

    // Torque first: zeroing a released axis records an origin the shaft is
    // free to drift away from before the first move.
    if (!m_enabled) setEnabled(true);

    if (m_link) m_link->send(axisproto::cmdZero(m_axisIndex));

    m_boardSteps        = 0;
    m_targetSteps       = 0;
    m_lastPolledSteps   = 0;
    m_stalledPolls      = 0;
    m_stallReported     = false;
    m_currentPositionMm = 0.0;
    m_halted  = false;
    m_isHomed = true;
    m_state   = State::Idle;

    StructuredLogger::instance().log(StructuredLogger::Category::Motion,
        "StepperAxisController",
        QString("Axis %1: zero set at current position.").arg(m_axisIndex));

    emit positionChanged(0.0);
    emit homed();
}

void StepperAxisController::tick(double targetUnits)
{
    if (!isIdentified() || !m_isHomed) return;
    if (m_halted || m_alarmed) return;      // latched: the board ignores us anyway
    if (m_state == State::Jogging) return;  // the operator has the axis

    const double clamped = clampToTravel(targetUnits);
    const long   before   = m_targetSteps;
    m_targetSteps = unitsToSteps(clamped);
    const bool entering = (m_state != State::Tracking);
    m_state = State::Tracking;

    // Only on entry and on a real change of target — this runs at 50Hz.
    if (entering || before != m_targetSteps) {
        if (entering) {
            StructuredLogger::instance().log(StructuredLogger::Category::Motion,
                "StepperAxisController",
                QString("Axis %1: tracking started, target %2 units -> %3 steps.")
                    .arg(m_axisIndex).arg(clamped, 0, 'f', 3).arg(m_targetSteps));
        }
    }

    // Sent EVERY tick, unchanged or not. This is not redundancy: the board's
    // watchdog watches the setpoint stream specifically, so a repeated target
    // is what tells it the host is still alive. Stop sending and it ramps the
    // axis down within 500ms, which is exactly what should happen if playback
    // dies mid-move.
    sendTarget();
}

void StepperAxisController::sendTarget()
{
    if (m_link) m_link->send(axisproto::cmdTarget(m_axisIndex, m_targetSteps));
}

void StepperAxisController::jogGantry(int stepsPerSec)
{
    if (!isIdentified()) return;
    if (m_alarmed) {
        emit errorOccurred("Cannot jog while the drive alarm is asserted.");
        return;
    }
    if (m_halted) clearFault();
    if (!m_enabled) setEnabled(true);

    const long requested = stepsPerSec;
    m_jogRate = static_cast<int>(std::clamp<long>(stepsPerSec, -m_vmax, m_vmax));
    m_state   = State::Jogging;
    if (m_link) m_link->send(axisproto::cmdJog(m_axisIndex, m_jogRate));

    // Logged because "jogging does nothing" has several causes that look
    // identical from the outside — a rate of zero, a rate clamped away by
    // vmax, or a command that never left the host at all. The number says
    // which.
    StructuredLogger::instance().log(StructuredLogger::Category::Motion,
        "StepperAxisController",
        QString("Axis %1: jog commanded, requested %2, sent %3 steps/s "
                "(vmax %4, enabled %5, halted %6)")
            .arg(m_axisIndex).arg(requested).arg(m_jogRate).arg(m_vmax)
            .arg(m_enabled ? "yes" : "no").arg(m_halted ? "yes" : "no"));
}

void StepperAxisController::stopJog()
{
    if (!isIdentified()) return;
    m_jogRate = 0;
    m_state   = State::Idle;
    if (m_link) m_link->send(axisproto::cmdJog(m_axisIndex, 0));
    StructuredLogger::instance().log(StructuredLogger::Category::Motion,
        "StepperAxisController", QString("Axis %1: jog stopped.").arg(m_axisIndex));

    // The board decelerates to a standstill and adopts wherever that lands as
    // its target. Follow it, or the next tick would command a jump back to the
    // pre-jog target.
    m_targetSteps = m_boardSteps;
}

void StepperAxisController::resetEncoder()
{
    if (!isIdentified()) return;
    if (m_link) m_link->send(axisproto::cmdZero(m_axisIndex));
    m_boardSteps        = 0;
    m_targetSteps       = 0;
    m_lastPolledSteps   = 0;
    m_currentPositionMm = 0.0;
    emit positionChanged(0.0);
}

void StepperAxisController::heartbeat()
{
    if (!isIdentified()) return;

    if (m_state == State::Jogging) {
        // Resend so the board's watchdog sees a live host, same reason as the
        // target stream in tick().
        if (m_link) m_link->send(axisproto::cmdJog(m_axisIndex, m_jogRate));
    }

    // Deliberately NOT resending the target when tracking. The whole value of
    // the board's watchdog is that a dead playback thread stops the axis; if
    // the heartbeat kept the setpoint alive, a hung host would drive the move
    // to completion with nobody watching.

    if (++m_ticksSincePoll >= POLL_EVERY_TICKS) {
        m_ticksSincePoll = 0;
        if (m_link) m_link->send(axisproto::cmdQuery(m_axisIndex));
    }
}

void StepperAxisController::onReply(const axisproto::Reply& reply)
{
    switch (reply.type) {

    case 'Q': {   // position, in steps the board has clocked out
        if (reply.args.isEmpty()) break;
        bool ok = false;
        const long steps = reply.args.at(0).toLong(&ok);
        if (!ok) break;

        m_boardSteps        = steps;
        m_currentPositionMm = stepsToUnits(steps);
        emit positionChanged(m_currentPositionMm);

        // Stall watch. This cannot see a motor losing steps — Q is the board's
        // own pulse count, so it agrees with itself by construction. What it
        // does catch is the board not acting on our targets at all: halted,
        // disabled, or not receiving them.
        const long gap = std::labs(m_targetSteps - steps);
        if (m_state == State::Tracking && gap > STALL_TOLERANCE_STEPS) {
            if (steps == m_lastPolledSteps) {
                if (++m_stalledPolls >= STALL_POLLS && !m_stallReported) {
                    m_stallReported = true;
                    const QString msg =
                        QString("Stepper is not following: commanded %1 steps, board is at %2 "
                                "and has not moved for %3ms.")
                            .arg(m_targetSteps).arg(steps)
                            .arg(STALL_POLLS * POLL_EVERY_TICKS * 20);
                    StructuredLogger::instance().log(StructuredLogger::Category::Safety,
                        "StepperAxisController", msg);
                    emit errorOccurred(msg);
                    emit driveStalled(m_targetSteps, steps);
                }
            } else {
                m_stalledPolls  = 0;
                m_stallReported = false;
            }
        } else {
            m_stalledPolls  = 0;
            m_stallReported = false;
        }
        m_lastPolledSteps = steps;
        break;
    }

    case 'S': {   // status word
        const auto info = axisproto::parseStatus(reply);
        if (!info) break;
        m_enabled = info->enabled();
        m_halted  = info->halted();
        if (info->alarm() && !m_alarmed) {
            m_alarmed = true;
            m_isHomed = false;
            emit alarmRaised("drive alarm (reported by status)");
        }
        break;
    }

    case '!': {   // asynchronous fault — arrives whenever the board decides
        const int code = reply.args.isEmpty() ? 0 : reply.args.at(0).toInt();
        const QString text = reply.args.size() > 1 ? reply.args.at(1) : QString("fault");

        const QString msg = QString("Stepper axis fault %1: %2").arg(code).arg(text);
        StructuredLogger::instance().log(StructuredLogger::Category::Safety,
            "StepperAxisController", msg);
        emit errorOccurred(msg);

        if (code == axisproto::FaultAlarm) {
            // The drive lost its own loop. The step count no longer describes
            // the shaft, so the origin is gone with it — clearing homed is
            // what stops playback and the travel clamp trusting a stale number.
            m_alarmed = true;
            m_halted  = true;
            m_isHomed = false;
            m_state   = State::Idle;
            emit alarmRaised(text);
        } else if (code == axisproto::FaultWatchdog) {
            // The board ramped the axis down and latched. It will ignore
            // everything until an explicit R.
            m_halted = true;
            m_state  = State::Idle;
        }
        break;
    }

    case 'H':   // no home switch fitted on this axis; see homeGantry()
    case 'A':
    default:
        break;
    }
}

void StepperAxisController::resetControlState()
{
    m_state           = State::Idle;
    m_targetSteps     = 0;
    m_boardSteps      = 0;
    m_lastPolledSteps = 0;
    m_jogRate         = 0;
    m_ticksSincePoll  = 0;
    m_stalledPolls    = 0;
    m_stallReported   = false;
    m_halted          = false;
    m_alarmed         = false;

    // The board keeps its step count in RAM, so opening the port — which
    // reboots it — puts the axis back at zero with no torque. Neither the
    // enable state nor the origin survives, and pretending otherwise would
    // let a reconnect command a move against a position that no longer means
    // anything.
    m_enabled = false;
}

void StepperAxisController::onLinkLostImpl()
{
    m_state   = State::Idle;
    m_jogRate = 0;
    m_enabled = false;
}
