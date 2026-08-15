// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Nucleus-M Protocol
// ═══════════════════════════════════════════════════════════════════════════════

#include "infrastructure/fiz/nucleusprotocol.h"
#include <algorithm>

uint16_t NucleusProtocol::percentToRaw(float percent)
{
    float clamped = std::clamp(percent, 0.0f, 100.0f);
    return static_cast<uint16_t>(clamped / 100.0f * VALUE_MAX);
}

float NucleusProtocol::rawToPercent(uint16_t raw)
{
    return static_cast<float>(raw) / VALUE_MAX * 100.0f;
}

uint8_t NucleusProtocol::calculateChecksum(const QByteArray& packet)
{
    if (packet.size() < 7) return 0;
    uint8_t sum = 0;
    for (int i = 1; i <= 6; ++i)
        sum += static_cast<uint8_t>(packet[i]);
    return sum;  // & 0xFF implicit in uint8_t
}

QByteArray NucleusProtocol::moveCommand(uint8_t motorId, float percent)
{
    uint16_t raw = percentToRaw(percent);

    QByteArray pkt(8, 0);
    pkt[0] = 0x3A;  // Start marker
    pkt[1] = 0x01;  // CMD_HIGH
    pkt[2] = 0x06;  // CMD_LOW (move)
    pkt[3] = static_cast<char>(motorId);
    pkt[4] = 0x75;  // FLAGS
    pkt[5] = static_cast<char>((raw >> 8) & 0xFF);  // VALUE_HIGH
    pkt[6] = static_cast<char>(raw & 0xFF);          // VALUE_LOW
    pkt[7] = static_cast<char>(calculateChecksum(pkt));

    return pkt;
}

QByteArray NucleusProtocol::focusCommand(float percent)
{
    return moveCommand(MOTOR_FOCUS, percent);
}

QByteArray NucleusProtocol::irisCommand(float percent)
{
    return moveCommand(MOTOR_IRIS, percent);
}

QByteArray NucleusProtocol::zoomCommand(float percent)
{
    return moveCommand(MOTOR_ZOOM, percent);
}

QByteArray NucleusProtocol::calibrateCommand(uint8_t motorId)
{
    QByteArray pkt(8, 0);
    pkt[0] = 0x3A;
    pkt[1] = 0x01;
    pkt[2] = 0x07;  // calibrate
    pkt[3] = static_cast<char>(motorId);
    pkt[4] = 0x00;
    pkt[5] = 0x00;
    pkt[6] = 0x00;
    pkt[7] = static_cast<char>(calculateChecksum(pkt));
    return pkt;
}

QByteArray NucleusProtocol::heartbeatPacket()
{
    // ASSUMPTION: verify against logic analyser output
    QByteArray pkt(8, 0);
    pkt[0] = 0x3A;
    pkt[1] = static_cast<char>(0x96);
    pkt[2] = 0x06;
    pkt[3] = 0x00;
    pkt[4] = 0x02;
    pkt[5] = 0x00;
    pkt[6] = 0x01;
    pkt[7] = 0x61; // checksum: 0x96+0x06+0x00+0x02+0x00+0x01 = 0x9F → 0x61?
    // ASSUMPTION: verify against logic analyser output — checksum may differ
    return pkt;
}
