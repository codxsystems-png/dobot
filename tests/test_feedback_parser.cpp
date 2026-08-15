// ═══════════════════════════════════════════════════════════════════════════════
// Test: FeedbackParser — verify 1440-byte binary packet parsing
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "core/feedback_parser.h"
#include <cstring>

class TestFeedbackParser : public QObject
{
    Q_OBJECT
private slots:
    void testPacketSize()
    {
        QCOMPARE(FeedbackParser::PACKET_SIZE, 1440);
    }

    void testShortPacket()
    {
        QByteArray shortPacket(100, '\0');
        auto fd = FeedbackParser::parse(shortPacket);
        QVERIFY(!fd.valid);
    }

    void testEmptyPacket()
    {
        QByteArray empty;
        auto fd = FeedbackParser::parse(empty);
        QVERIFY(!fd.valid);
    }

    void testValidPacketStructure()
    {
        // Create a synthetic 1440-byte packet with known values
        QByteArray packet(1440, '\0');
        char* d = packet.data();

        // Set packet length at offset 0 (uint16_t = 1440)
        uint16_t len = 1440;
        std::memcpy(d + 0, &len, 2);

        // Set robot mode at offset 24 (uint64_t = 5 = Idle)
        uint64_t mode = 5;
        std::memcpy(d + 24, &mode, 8);

        // Set actual joint 0 at offset 432 (double = 45.0)
        double j0 = 45.0;
        std::memcpy(d + 432, &j0, 8);

        // Set actual joint 1 at offset 440 (double = -30.0)
        double j1 = -30.0;
        std::memcpy(d + 440, &j1, 8);

        // Set Cartesian X at offset 624 (double = 500.0)
        double cx = 500.0;
        std::memcpy(d + 624, &cx, 8);

        // Set Cartesian Y at offset 632 (double = 100.5)
        double cy = 100.5;
        std::memcpy(d + 632, &cy, 8);

        // Set velocity ratio at offset 1016 (int8 = 80)
        d[1016] = 80;

        auto fd = FeedbackParser::parse(packet);
        QVERIFY(fd.valid);
        QCOMPARE(fd.packetLength, static_cast<uint16_t>(1440));
        QCOMPARE(fd.robotModeRaw, static_cast<uint64_t>(5));
        QCOMPARE(fd.robotMode(), RobotMode::Idle);
        QCOMPARE(fd.actualJoints.j[0], 45.0);
        QCOMPARE(fd.actualJoints.j[1], -30.0);
        QCOMPARE(fd.actualPose.x, 500.0);
        QCOMPARE(fd.actualPose.y, 100.5);
        QCOMPARE(fd.velocityRatio, static_cast<int8_t>(80));
    }

    void testRobotModeMapping()
    {
        QByteArray packet(1440, '\0');
        char* d = packet.data();

        // Test each valid robot mode
        for (int m = 1; m <= 11; ++m) {
            uint64_t mode = static_cast<uint64_t>(m);
            std::memcpy(d + 24, &mode, 8);

            auto fd = FeedbackParser::parse(packet);
            QVERIFY(fd.valid);
            QCOMPARE(static_cast<int>(fd.robotMode()), m);
        }
    }

    void testAllJointAngles()
    {
        QByteArray packet(1440, '\0');
        char* d = packet.data();

        double joints[] = {10.0, 20.0, 30.0, 40.0, 50.0, 60.0};
        for (int i = 0; i < 6; ++i)
            std::memcpy(d + 432 + i * 8, &joints[i], 8);

        auto fd = FeedbackParser::parse(packet);
        QVERIFY(fd.valid);
        for (int i = 0; i < 6; ++i)
            QCOMPARE(fd.actualJoints.j[i], joints[i]);
    }

    void testCartesianPose()
    {
        QByteArray packet(1440, '\0');
        char* d = packet.data();

        double values[] = {100.0, 200.0, 300.0, 45.0, 90.0, 135.0};
        for (int i = 0; i < 6; ++i)
            std::memcpy(d + 624 + i * 8, &values[i], 8);

        auto fd = FeedbackParser::parse(packet);
        QVERIFY(fd.valid);
        QCOMPARE(fd.actualPose.x,  100.0);
        QCOMPARE(fd.actualPose.y,  200.0);
        QCOMPARE(fd.actualPose.z,  300.0);
        QCOMPARE(fd.actualPose.rx, 45.0);
        QCOMPARE(fd.actualPose.ry, 90.0);
        QCOMPARE(fd.actualPose.rz, 135.0);
    }

    void testDigitalIO()
    {
        QByteArray packet(1440, '\0');
        char* d = packet.data();

        uint64_t di = 0xFF00;
        uint64_t doVal = 0x00FF;
        std::memcpy(d + 8, &di, 8);
        std::memcpy(d + 16, &doVal, 8);

        auto fd = FeedbackParser::parse(packet);
        QVERIFY(fd.valid);
        QCOMPARE(fd.digitalInputs, static_cast<uint64_t>(0xFF00));
        QCOMPARE(fd.digitalOutputs, static_cast<uint64_t>(0x00FF));
    }

    void testReadHelpers()
    {
        char data[16] = {};
        double val = 3.14159;
        std::memcpy(data, &val, 8);

        QCOMPARE(FeedbackParser::readDouble(data, 0, 16), 3.14159);
        QCOMPARE(FeedbackParser::readDouble(data, 12, 16), 0.0); // out of bounds
    }
};

QTEST_APPLESS_MAIN(TestFeedbackParser)
#include "test_feedback_parser.moc"
