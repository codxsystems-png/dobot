// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Dobot ASCII Response Parser
// ═══════════════════════════════════════════════════════════════════════════════

#include "core/response_parser.h"
#include <QRegularExpression>

namespace ResponseParser {

ParsedResponse parse(const QString& response)
{
    ParsedResponse result;
    result.rawResponse = response;

    // Remove trailing semicolon and whitespace
    QString clean = response.trimmed();
    if (clean.endsWith(';'))
        clean.chop(1);
    clean = clean.trimmed();

    if (clean.isEmpty())
        return result;

    // Find the first comma — everything before it is the ErrorID
    int firstComma = clean.indexOf(',');
    if (firstComma < 0) {
        // Try single-value response (just error ID)
        bool ok = false;
        result.errorId = clean.toInt(&ok);
        result.valid = ok;
        return result;
    }

    // Parse ErrorID
    bool ok = false;
    result.errorId = clean.left(firstComma).trimmed().toInt(&ok);
    if (!ok)
        return result;

    // Find { } block
    int braceOpen = clean.indexOf('{', firstComma);
    int braceClose = clean.indexOf('}', braceOpen > 0 ? braceOpen : firstComma);

    if (braceOpen > 0 && braceClose > braceOpen) {
        // Extract values between braces
        QString valStr = clean.mid(braceOpen + 1, braceClose - braceOpen - 1).trimmed();
        if (!valStr.isEmpty()) {
            result.values = valStr.split(',', Qt::SkipEmptyParts);
            for (auto& v : result.values)
                v = v.trimmed();
        }

        // Everything after the closing brace + comma is the command name
        int cmdStart = braceClose + 1;
        if (cmdStart < clean.length() && clean[cmdStart] == ',')
            cmdStart++;
        QString cmdPart = clean.mid(cmdStart).trimmed();

        // Extract command name (strip parameters in parentheses)
        int parenPos = cmdPart.indexOf('(');
        if (parenPos > 0)
            result.commandName = cmdPart.left(parenPos).trimmed();
        else
            result.commandName = cmdPart.trimmed();
    } else {
        // No braces — might be "ErrorID,CommandName(params);" format
        QString remainder = clean.mid(firstComma + 1).trimmed();
        int parenPos = remainder.indexOf('(');
        if (parenPos > 0)
            result.commandName = remainder.left(parenPos).trimmed();
        else
            result.commandName = remainder.trimmed();
    }

    result.valid = true;
    return result;
}

int extractInt(const ParsedResponse& r, int index)
{
    if (index < 0 || index >= r.values.size())
        return -1;
    bool ok = false;
    int val = r.values[index].toInt(&ok);
    return ok ? val : -1;
}

double extractDouble(const ParsedResponse& r, int index)
{
    if (index < 0 || index >= r.values.size())
        return 0.0;
    bool ok = false;
    double val = r.values[index].toDouble(&ok);
    return ok ? val : 0.0;
}

} // namespace ResponseParser
