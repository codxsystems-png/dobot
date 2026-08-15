#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Dobot ASCII Response Parser
// Parses: "ErrorID,{val1,val2,...},CommandName(params);"
// Reference: dobot/DOBOT_TCP-IP.pdf
// ═══════════════════════════════════════════════════════════════════════════════

#include <QString>
#include <QStringList>

struct ParsedResponse {
    int         errorId   = -1;       // 0 = success, >0 = error code
    QStringList values;               // contents between { }
    QString     commandName;          // e.g. "MovJ" or "EnableRobot"
    QString     rawResponse;          // original full string
    bool        valid     = false;    // true if parsing succeeded
};

namespace ResponseParser {

/// Parse a Dobot ASCII response string.
/// Expected format: "ErrorID,{val1,val2,...},CommandName(params);"
/// Also handles simpler responses like "ErrorID,{val1,val2},CommandName;"
ParsedResponse parse(const QString& response);

/// Extract a single integer return value (e.g., from GetCurrentCommandID).
/// Returns -1 if not available.
int extractInt(const ParsedResponse& r, int index = 0);

/// Extract a single double return value.
/// Returns 0.0 if not available.
double extractDouble(const ParsedResponse& r, int index = 0);

} // namespace ResponseParser
