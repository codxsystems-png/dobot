#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Binary Feedback Parser (Port 30004)
// Parses 1440-byte little-endian binary packets from Dobot Nova 5.
// Offsets verified from: dobot/TCP-IP-ROS-6AXis-main/.../commander.h
// ═══════════════════════════════════════════════════════════════════════════════

#include <QByteArray>
#include "types.h"
#include <cstdint>

/// Parsed result from a single 1440-byte feedback packet
struct FeedbackData {
    uint16_t      packetLength = 0;     // bytes 0-1 (should be 1440)
    uint64_t      digitalInputs = 0;    // bytes 8-15
    uint64_t      digitalOutputs = 0;   // bytes 16-23
    uint64_t      robotModeRaw = 0;     // bytes 24-31
    uint64_t      controllerTimer = 0;  // bytes 32-39
    double        speedScaling = 0.0;   // bytes 64-71

    JointAngles   targetJoints;         // bytes 192-239 (q_target, degrees)
    JointAngles   actualJoints;         // bytes 432-479 (q_actual, degrees)

    CartesianPose actualPose;           // bytes 624-671 (tool_vector_actual)

    double        motorTemperatures[6] = {}; // bytes 864-911

    int8_t        velocityRatio = 0;    // byte 1016
    int8_t        brakeStatus = 0;      // byte 1025
    int8_t        enableStatus = 0;     // byte 1026
    int8_t        dragStatus = 0;       // byte 1027
    int8_t        runningStatus = 0;    // byte 1028
    int8_t        errorStatus = 0;      // byte 1029

    bool          valid = false;        // true if parsed successfully

    /// Convert raw robot_mode uint64 to our RobotMode enum
    RobotMode robotMode() const {
        int mode = static_cast<int>(robotModeRaw);
        if (mode >= 1 && mode <= 11)
            return static_cast<RobotMode>(mode);
        return RobotMode::Init; // safe default
    }
};

namespace FeedbackParser {

/// Expected size of a complete feedback packet
constexpr int PACKET_SIZE = 1440;

/// Parse a 1440-byte binary feedback packet.
/// Returns FeedbackData with valid=true if packet size is correct.
/// Data is little-endian (x86 native).
FeedbackData parse(const QByteArray& packet);

/// Extract a little-endian double from raw bytes at the given offset.
/// Returns 0.0 if offset + 8 exceeds data size.
double readDouble(const char* data, int offset, int dataSize);

/// Extract a little-endian uint64 from raw bytes at the given offset.
uint64_t readUint64(const char* data, int offset, int dataSize);

/// Extract a little-endian uint16 from raw bytes at the given offset.
uint16_t readUint16(const char* data, int offset, int dataSize);

/// Extract a signed int8 from raw bytes at the given offset.
int8_t readInt8(const char* data, int offset, int dataSize);

} // namespace FeedbackParser
