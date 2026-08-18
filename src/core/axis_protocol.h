#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Axis Board Protocol v2
//
// Wire format for the Arduino axis board. Replaces the v1 protocol, whose
// single-character commands carried no axis address and whose replies were
// bare values matched positionally against "whatever the host asked last" —
// so any unsolicited line was silently mis-attributed to the pending query.
//
// v2 fixes both: every command names an axis, and every reply begins with a
// type character, so the host parses by content and never by expectation.
// Unrecognised line types are discarded, which is also the forward-
// compatibility escape hatch for future firmware.
//
// Pure string handling — no Qt widgets, no serial, no hardware — so the wire
// format is unit-testable on its own. Mirrors the existing command_builder /
// response_parser split used for the Dobot TCP side.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <optional>

namespace axisproto {

/// Protocol major version this build speaks. A mismatch against the board's
/// reported PROTO is refused outright rather than tolerated — the operator
/// flashes firmware by hand, so drift is likely and silence would be worse.
constexpr int kProtocolVersion = 2;

// ─── Replies ──────────────────────────────────────────────────────────────

/// Firmware → host. `type` is the leading character; `axis` is -1 for global
/// replies (V, P, #) that carry no axis.
struct Reply {
    char        type = '\0';
    int         axis = -1;
    QStringList args;
    QString     raw;
};

/// Status flags in an `S` reply's hex field.
enum StatusFlag : int {
    StatusEnabled         = 0x01,
    StatusMoving          = 0x02,
    StatusHomeSwitch      = 0x04,
    StatusAlarm           = 0x08,
    StatusHalted          = 0x10,
    StatusPositionLost    = 0x20,
    StatusWatchdogTripped = 0x40
};

/// Fault codes in a `!` reply.
enum FaultCode : int {
    FaultAlarm    = 1,   // drive asserted ALM — position is no longer trusted
    FaultWatchdog = 2,   // setpoint stream stopped
    FaultLimit    = 3,
    FaultBadCmd   = 4,
    FaultOverRate = 5    // requested step rate above the board's ceiling
};

struct VersionInfo {
    int         firmware = 0;
    int         protocol = 0;
    QString     board;
    int         axisCount = 0;
    QStringList caps;
};

struct StatusInfo {
    int    flags = 0;
    long   position = 0;
    double rate = 0.0;

    bool enabled()      const { return flags & StatusEnabled; }
    bool moving()       const { return flags & StatusMoving; }
    bool homeSwitch()   const { return flags & StatusHomeSwitch; }
    bool alarm()        const { return flags & StatusAlarm; }
    bool halted()       const { return flags & StatusHalted; }
    bool positionLost() const { return flags & StatusPositionLost; }
    bool watchdog()     const { return flags & StatusWatchdogTripped; }
};

/// Parses one line from the board. Returns nullopt for blank lines, comments
/// (`#`) and anything whose type character isn't recognised — callers log and
/// discard those rather than guessing at their meaning.
std::optional<Reply> parseLine(const QString& line);

/// Interprets a `V` reply. nullopt if it isn't one, or is malformed.
std::optional<VersionInfo> parseVersion(const Reply& reply);

/// Interprets an `S` reply. nullopt if it isn't one, or is malformed.
std::optional<StatusInfo> parseStatus(const Reply& reply);

/// True when the board's protocol major matches what this build speaks.
bool isCompatible(const VersionInfo& info);

// ─── Commands ─────────────────────────────────────────────────────────────
// Each returns a complete newline-terminated line ready for the transport.

QByteArray cmdVersion();                                    // V
QByteArray cmdEnumerate();                                  // A
QByteArray cmdPing();                                       // P
QByteArray cmdStopAll();                                    // X

QByteArray cmdPwm(int axis, int pwm);                       // G <ax> <pwm>   DC drive
QByteArray cmdTarget(int axis, long steps);                 // T <ax> <steps> stepper target
QByteArray cmdJog(int axis, long stepsPerSec);              // J <ax> <rate>
QByteArray cmdLimits(int axis, long vmax, long amax);       // L <ax> <vmax> <amax>
QByteArray cmdEnable(int axis, bool on);                    // E <ax> <0|1>
QByteArray cmdQuery(int axis);                              // Q <ax>
QByteArray cmdZero(int axis);                               // Z <ax>
QByteArray cmdHome(int axis);                               // H <ax>
QByteArray cmdStatus(int axis);                             // S <ax>
QByteArray cmdResume(int axis);                             // R <ax>

} // namespace axisproto
