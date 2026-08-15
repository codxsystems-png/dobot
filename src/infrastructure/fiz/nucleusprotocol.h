#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Nucleus-M Protocol (Phase 7)
// Static 8-byte packet builder. Pure functions, no QObject.
// Protocol: Community reverse-engineered. ASSUMPTION markers where unverified.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QByteArray>
#include <cstdint>

class NucleusProtocol
{
public:
    /// Motor IDs
    static constexpr uint8_t MOTOR_FOCUS = 0x01;
    static constexpr uint8_t MOTOR_IRIS  = 0x02;
    static constexpr uint8_t MOTOR_ZOOM  = 0x03;

    /// Value range
    static constexpr uint16_t VALUE_MIN = 0x0000;  // 0%
    static constexpr uint16_t VALUE_MAX = 0x7FFF;  // 100%

    /// Build a motor move command (8 bytes)
    /// [0x3A, 0x01, 0x06, motorId, 0x75, valueHigh, valueLow, checksum]
    static QByteArray moveCommand(uint8_t motorId, float percent);

    /// Convenience wrappers
    static QByteArray focusCommand(float percent);
    static QByteArray irisCommand(float percent);
    static QByteArray zoomCommand(float percent);

    /// Calibrate a motor
    /// [0x3A, 0x01, 0x07, motorId, 0x00, 0x00, 0x00, checksum]
    static QByteArray calibrateCommand(uint8_t motorId);

    /// Heartbeat packet (send every 1000 ms to keep motors engaged)
    /// Raw: 3A 96 06 00 02 00 01 61
    /// ASSUMPTION: verify against logic analyser output
    static QByteArray heartbeatPacket();

    /// Convert percent (0.0–100.0) to raw 16-bit value (0x0000–0x7FFF)
    /// Clamps input to [0.0, 100.0]
    static uint16_t percentToRaw(float percent);

    /// Convert raw 16-bit value back to percent
    static float rawToPercent(uint16_t raw);

    /// Calculate checksum: (byte[1]+byte[2]+byte[3]+byte[4]+byte[5]+byte[6]) & 0xFF
    static uint8_t calculateChecksum(const QByteArray& packet);
};
