// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Binary Feedback Parser (Port 30004)
// ═══════════════════════════════════════════════════════════════════════════════

#include "core/feedback_parser.h"
#include <QtEndian>
#include <cstring>

namespace FeedbackParser {

// ─── Byte-level readers (little-endian, safe bounds) ────────────────────────

double readDouble(const char* data, int offset, int dataSize)
{
    if (offset + 8 > dataSize) return 0.0;
    double val = 0.0;
    std::memcpy(&val, data + offset, 8);
    // x86 is natively little-endian, so no byte-swap needed.
    // On big-endian platforms, use qFromLittleEndian.
    return val;
}

uint64_t readUint64(const char* data, int offset, int dataSize)
{
    if (offset + 8 > dataSize) return 0;
    uint64_t val = 0;
    std::memcpy(&val, data + offset, 8);
    return val;
}

uint16_t readUint16(const char* data, int offset, int dataSize)
{
    if (offset + 2 > dataSize) return 0;
    uint16_t val = 0;
    std::memcpy(&val, data + offset, 2);
    return val;
}

int8_t readInt8(const char* data, int offset, int dataSize)
{
    if (offset >= dataSize) return 0;
    return static_cast<int8_t>(data[offset]);
}

// ─── Main parser ────────────────────────────────────────────────────────────

FeedbackData parse(const QByteArray& packet)
{
    FeedbackData fd;

    if (packet.size() < PACKET_SIZE) {
        fd.valid = false;
        return fd;
    }

    const char* d = packet.constData();
    const int sz  = packet.size();

    // Header
    fd.packetLength   = readUint16(d, 0, sz);
    fd.digitalInputs  = readUint64(d, 8, sz);
    fd.digitalOutputs = readUint64(d, 16, sz);
    fd.robotModeRaw   = readUint64(d, 24, sz);
    fd.controllerTimer= readUint64(d, 32, sz);
    fd.speedScaling   = readDouble(d, 64, sz);

    // Target joint angles — offset 192, 6×double (degrees)
    for (int i = 0; i < 6; ++i)
        fd.targetJoints.j[i] = readDouble(d, 192 + i * 8, sz);

    // Actual joint angles — offset 432, 6×double (degrees)
    // NOTE: The Dobot reference code stores these as radians internally
    // but the raw feedback data is in degrees from the wire.
    // ASSUMPTION: verify against hardware — if values look like radians,
    // multiply by (180.0 / 3.14159265358979).
    for (int i = 0; i < 6; ++i)
        fd.actualJoints.j[i] = readDouble(d, 432 + i * 8, sz);

    // Actual Cartesian pose — offset 624, 6×double (x,y,z in mm, rx,ry,rz in degrees)
    fd.actualPose.x  = readDouble(d, 624, sz);
    fd.actualPose.y  = readDouble(d, 632, sz);
    fd.actualPose.z  = readDouble(d, 640, sz);
    fd.actualPose.rx = readDouble(d, 648, sz);
    fd.actualPose.ry = readDouble(d, 656, sz);
    fd.actualPose.rz = readDouble(d, 664, sz);

    // Motor temperatures — offset 864, 6×double
    for (int i = 0; i < 6; ++i)
        fd.motorTemperatures[i] = readDouble(d, 864 + i * 8, sz);

    // Status bytes
    fd.velocityRatio = readInt8(d, 1016, sz);
    fd.brakeStatus   = readInt8(d, 1025, sz);
    fd.enableStatus  = readInt8(d, 1026, sz);
    fd.dragStatus    = readInt8(d, 1027, sz);
    fd.runningStatus = readInt8(d, 1028, sz);
    fd.errorStatus   = readInt8(d, 1029, sz);

    fd.valid = true;
    return fd;
}

} // namespace FeedbackParser
