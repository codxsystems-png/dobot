#include "core/axis_protocol.h"

namespace axisproto {

namespace {

/// Reply types that carry an axis index as their first argument. Everything
/// else is global.
bool typeCarriesAxis(char type)
{
    switch (type) {
    case 'A': case 'Q': case 'H': case 'S': case '!':
        return true;
    default:
        return false;
    }
}

bool isKnownReplyType(char type)
{
    switch (type) {
    case 'V': case 'A': case 'Q': case 'H': case 'S': case 'P': case '!':
        return true;
    default:
        return false;
    }
}

} // namespace

std::optional<Reply> parseLine(const QString& line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) return std::nullopt;

    // Comments are for humans reading a serial monitor; the host ignores them.
    if (trimmed.startsWith('#')) return std::nullopt;

    const QChar typeChar = trimmed.at(0);
    if (!isKnownReplyType(typeChar.toLatin1())) return std::nullopt;

    // The type character may or may not be followed by a space; either way the
    // first token is the type and the rest are arguments.
    QStringList tokens = trimmed.split(' ', Qt::SkipEmptyParts);
    if (tokens.isEmpty()) return std::nullopt;

    Reply r;
    r.type = typeChar.toLatin1();
    r.raw  = trimmed;

    // Drop the leading type token — but only the type character itself, since
    // a form like "V FW=2" has the type alone in token 0.
    tokens.removeFirst();

    if (typeCarriesAxis(r.type)) {
        if (tokens.isEmpty()) return std::nullopt;   // axis is mandatory here
        bool ok = false;
        const int axis = tokens.first().toInt(&ok);
        if (!ok || axis < 0) return std::nullopt;
        r.axis = axis;
        tokens.removeFirst();
    }

    r.args = tokens;
    return r;
}

std::optional<VersionInfo> parseVersion(const Reply& reply)
{
    if (reply.type != 'V') return std::nullopt;

    VersionInfo info;
    bool sawFirmware = false;
    bool sawProtocol = false;

    for (const QString& token : reply.args) {
        const int eq = token.indexOf('=');
        if (eq <= 0) continue;
        const QString key   = token.left(eq);
        const QString value = token.mid(eq + 1);

        bool ok = false;
        if (key == "FW") {
            info.firmware = value.toInt(&ok);
            sawFirmware = ok;
        } else if (key == "PROTO") {
            info.protocol = value.toInt(&ok);
            sawProtocol = ok;
        } else if (key == "BOARD") {
            info.board = value;
        } else if (key == "AXES") {
            info.axisCount = value.toInt();
        } else if (key == "CAPS") {
            info.caps = value.split(',', Qt::SkipEmptyParts);
        }
    }

    // FW and PROTO are the two the host actually acts on; without them the
    // line is not a usable identification.
    if (!sawFirmware || !sawProtocol) return std::nullopt;
    return info;
}

std::optional<StatusInfo> parseStatus(const Reply& reply)
{
    if (reply.type != 'S') return std::nullopt;
    if (reply.args.size() < 3) return std::nullopt;

    bool flagsOk = false;
    bool posOk   = false;
    bool rateOk  = false;

    StatusInfo s;
    s.flags    = reply.args.at(0).toInt(&flagsOk, 16);
    s.position = reply.args.at(1).toLong(&posOk);
    s.rate     = reply.args.at(2).toDouble(&rateOk);

    if (!flagsOk || !posOk || !rateOk) return std::nullopt;
    return s;
}

bool isCompatible(const VersionInfo& info)
{
    return info.protocol == kProtocolVersion;
}

// ─── Commands ─────────────────────────────────────────────────────────────

namespace {
QByteArray line(const QString& s) { return s.toUtf8() + '\n'; }
}

QByteArray cmdVersion()   { return line("V"); }
QByteArray cmdEnumerate() { return line("A"); }
QByteArray cmdPing()      { return line("P"); }
QByteArray cmdStopAll()   { return line("X"); }

QByteArray cmdTarget(int axis, long steps)
{
    return line(QString("T %1 %2").arg(axis).arg(steps));
}

QByteArray cmdJog(int axis, long stepsPerSec)
{
    return line(QString("J %1 %2").arg(axis).arg(stepsPerSec));
}

QByteArray cmdLimits(int axis, long vmax, long amax)
{
    return line(QString("L %1 %2 %3").arg(axis).arg(vmax).arg(amax));
}

QByteArray cmdEnable(int axis, bool on)
{
    return line(QString("E %1 %2").arg(axis).arg(on ? 1 : 0));
}

QByteArray cmdQuery(int axis)  { return line(QString("Q %1").arg(axis)); }
QByteArray cmdZero(int axis)   { return line(QString("Z %1").arg(axis)); }
QByteArray cmdHome(int axis)   { return line(QString("H %1").arg(axis)); }
QByteArray cmdStatus(int axis) { return line(QString("S %1").arg(axis)); }
QByteArray cmdResume(int axis) { return line(QString("R %1").arg(axis)); }

} // namespace axisproto
