#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Dobot ASCII Command Builder
// Generates newline-terminated command strings for port 29999 / 30003.
// Reference: dobot/DOBOT_TCP-IP.pdf
// ═══════════════════════════════════════════════════════════════════════════════

#include <QString>
#include "types.h"

namespace CommandBuilder {

// ─── Dashboard Commands (port 29999) ────────────────────────────────────────
inline QString enableRobot()                { return "EnableRobot()\n"; }
inline QString disableRobot()               { return "DisableRobot()\n"; }
inline QString clearError()                 { return "ClearError()\n"; }
inline QString resetRobot()                 { return "ResetRobot()\n"; }
inline QString powerOn()                    { return "PowerOn()\n"; }
inline QString robotMode()                  { return "RobotMode()\n"; }
inline QString emergencyStop(int enable)    { return QString("EmergencyStop(%1)\n").arg(enable); }
inline QString speedFactor(int pct)         { return QString("SpeedFactor(%1)\n").arg(qBound(1, pct, 100)); }
inline QString getErrorID()                 { return "GetErrorID()\n"; }
inline QString user(int index)              { return QString("User(%1)\n").arg(index); }
inline QString tool(int index)              { return QString("Tool(%1)\n").arg(index); }
inline QString setCollisionLevel(int level) { return QString("SetCollisionLevel(%1)\n").arg(qBound(0, level, 5)); }
inline QString getAngle()                   { return "GetAngle()\n"; }
inline QString getPose()                    { return "GetPose()\n"; }

// Speed / acceleration
inline QString accJ(int pct)   { return QString("AccJ(%1)\n").arg(qBound(1, pct, 100)); }
inline QString accL(int pct)   { return QString("AccL(%1)\n").arg(qBound(1, pct, 100)); }
inline QString velJ(int pct)   { return QString("VelJ(%1)\n").arg(qBound(1, pct, 100)); }
inline QString velL(int pct)   { return QString("VelL(%1)\n").arg(qBound(1, pct, 100)); }
inline QString cp(double val)  { return QString("CP(%1)\n").arg(val, 0, 'f', 2); }

// Drag mode
inline QString startDrag()  { return "StartDrag()\n"; }
inline QString stopDrag()   { return "StopDrag()\n"; }

// Digital I/O
inline QString doCmd(int index, int status)        { return QString("DO(%1,%2)\n").arg(index).arg(status); }
inline QString doInstant(int index, int status)    { return QString("DOInstant(%1,%2)\n").arg(index).arg(status); }
inline QString toolDO(int index, int status)       { return QString("ToolDO(%1,%2)\n").arg(index).arg(status); }
inline QString toolDOInstant(int index, int status){ return QString("ToolDOInstant(%1,%2)\n").arg(index).arg(status); }

// Calibration
inline QString setUser(int index, const QString& params) { return QString("SetUser(%1,%2)\n").arg(index).arg(params); }
inline QString setTool(int index, const QString& params) { return QString("SetTool(%1,%2)\n").arg(index).arg(params); }

// ─── Motion Commands (port 29999 or 30003) ──────────────────────────────────

/// MovJ — Joint move to Cartesian target
/// speedPct & accPct are clamped 1..100, cpValue 0.0+ (0 = stop at point)
inline QString movJ(const CartesianPose& p, int speedPct = 80, int accPct = 50, double cpValue = 0.0)
{
    return QString("MovJ(%1,%2,%3,%4,%5,%6,SpeedJ=%7,AccJ=%8,CP=%9)\n")
        .arg(p.x, 0, 'f', 4).arg(p.y, 0, 'f', 4).arg(p.z, 0, 'f', 4)
        .arg(p.rx, 0, 'f', 4).arg(p.ry, 0, 'f', 4).arg(p.rz, 0, 'f', 4)
        .arg(qBound(1, speedPct, 100))
        .arg(qBound(1, accPct, 100))
        .arg(cpValue, 0, 'f', 2);
}

/// MovL — Linear move to Cartesian target
inline QString movL(const CartesianPose& p, int speedPct = 80, int accPct = 50, double cpValue = 0.0)
{
    return QString("MovL(%1,%2,%3,%4,%5,%6,SpeedL=%7,AccL=%8,CP=%9)\n")
        .arg(p.x, 0, 'f', 4).arg(p.y, 0, 'f', 4).arg(p.z, 0, 'f', 4)
        .arg(p.rx, 0, 'f', 4).arg(p.ry, 0, 'f', 4).arg(p.rz, 0, 'f', 4)
        .arg(qBound(1, speedPct, 100))
        .arg(qBound(1, accPct, 100))
        .arg(cpValue, 0, 'f', 2);
}

/// Arc — Arc move through via-point to end-point (start = current position)
inline QString arc(const CartesianPose& via, const CartesianPose& end,
                   int speedPct = 80, int accPct = 50, double cpValue = 0.0)
{
    return QString("Arc(%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,SpeedL=%13,AccL=%14,CP=%15)\n")
        .arg(via.x, 0, 'f', 4).arg(via.y, 0, 'f', 4).arg(via.z, 0, 'f', 4)
        .arg(via.rx, 0, 'f', 4).arg(via.ry, 0, 'f', 4).arg(via.rz, 0, 'f', 4)
        .arg(end.x, 0, 'f', 4).arg(end.y, 0, 'f', 4).arg(end.z, 0, 'f', 4)
        .arg(end.rx, 0, 'f', 4).arg(end.ry, 0, 'f', 4).arg(end.rz, 0, 'f', 4)
        .arg(qBound(1, speedPct, 100))
        .arg(qBound(1, accPct, 100))
        .arg(cpValue, 0, 'f', 2);
}

/// MoveJog — Jog a single axis. Pass "" to stop.
inline QString moveJog(const QString& axis = "")
{
    return QString("MoveJog(%1)\n").arg(axis);
}

/// GetCurrentCommandID — for ResultID tracking
inline QString getCurrentCommandID() { return "GetCurrentCommandID()\n"; }

/// ServoJ — Joint-space servo command
inline QString servoJ(const JointAngles& j, double t = 0.008)
{
    return QString("ServoJ(%1,%2,%3,%4,%5,%6,%7)\n")
        .arg(j.j[0], 0, 'f', 4).arg(j.j[1], 0, 'f', 4).arg(j.j[2], 0, 'f', 4)
        .arg(j.j[3], 0, 'f', 4).arg(j.j[4], 0, 'f', 4).arg(j.j[5], 0, 'f', 4)
        .arg(t, 0, 'f', 4);
}

/// ServoP — Cartesian-space servo command, mirrors servoJ()'s parameter
/// convention (t = interval seconds, default 8ms per the design spec's
/// realtime feedback cadence).
/// ASSUMPTION: verify parameter order/name against dobot/DOBOT_TCP-IP.pdf —
/// unlike movJ/movL/arc (already exercised against real hardware per Gate 1),
/// this hasn't been confirmed. Also unconfirmed: whether ServoP must be sent
/// over the realtime port (30003, per this file's header comment) rather
/// than the dashboard port (29999) that DobotAdapter currently has access
/// to — see DobotAdapter::sendStreamedSetpoint().
inline QString servoP(const CartesianPose& p, double t = 0.008)
{
    return QString("ServoP(%1,%2,%3,%4,%5,%6,%7)\n")
        .arg(p.x, 0, 'f', 4).arg(p.y, 0, 'f', 4).arg(p.z, 0, 'f', 4)
        .arg(p.rx, 0, 'f', 4).arg(p.ry, 0, 'f', 4).arg(p.rz, 0, 'f', 4)
        .arg(t, 0, 'f', 4);
}

/// Stop current motion
inline QString stop() { return "Stop()\n"; }

/// Pause current motion
inline QString pause() { return "Pause()\n"; }

/// Continue paused motion
inline QString continueCmd() { return "Continue()\n"; }

// ─── Modbus (for external devices) ──────────────────────────────────────────
inline QString setTool485(const QString& baud, const QString& parity)
{
    return QString("SetTool485(%1,%2)\n").arg(baud).arg(parity);
}

inline QString setToolPower(int status) { return QString("SetToolPower(%1)\n").arg(status); }

} // namespace CommandBuilder
