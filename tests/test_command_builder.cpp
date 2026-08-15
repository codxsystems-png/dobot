// ═══════════════════════════════════════════════════════════════════════════════
// Test: CommandBuilder — verify ASCII command string generation
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "core/command_builder.h"

class TestCommandBuilder : public QObject
{
    Q_OBJECT
private slots:
    void testEnableRobot()
    {
        QCOMPARE(CommandBuilder::enableRobot(), QString("EnableRobot()\n"));
    }

    void testEmergencyStop()
    {
        QCOMPARE(CommandBuilder::emergencyStop(1), QString("EmergencyStop(1)\n"));
        QCOMPARE(CommandBuilder::emergencyStop(0), QString("EmergencyStop(0)\n"));
    }

    void testSpeedFactor()
    {
        QCOMPARE(CommandBuilder::speedFactor(80), QString("SpeedFactor(80)\n"));
        // Clamping
        QCOMPARE(CommandBuilder::speedFactor(0), QString("SpeedFactor(1)\n"));
        QCOMPARE(CommandBuilder::speedFactor(150), QString("SpeedFactor(100)\n"));
    }

    void testMovJ()
    {
        CartesianPose p{500.0, 0.0, 300.0, 180.0, 0.0, 90.0};
        QString cmd = CommandBuilder::movJ(p, 80, 50, 0.0);
        QVERIFY(cmd.startsWith("MovJ("));
        QVERIFY(cmd.contains("500.0000"));
        QVERIFY(cmd.contains("SpeedJ=80"));
        QVERIFY(cmd.contains("AccJ=50"));
        QVERIFY(cmd.contains("CP=0.00"));
        QVERIFY(cmd.endsWith(")\n"));
    }

    void testMovL()
    {
        CartesianPose p{100.0, 200.0, 300.0, 0.0, 0.0, 0.0};
        QString cmd = CommandBuilder::movL(p, 50, 30, 5.0);
        QVERIFY(cmd.startsWith("MovL("));
        QVERIFY(cmd.contains("SpeedL=50"));
        QVERIFY(cmd.contains("CP=5.00"));
    }

    void testMoveJog()
    {
        QCOMPARE(CommandBuilder::moveJog("J1+"), QString("MoveJog(J1+)\n"));
        QCOMPARE(CommandBuilder::moveJog(""),    QString("MoveJog()\n"));
        QCOMPARE(CommandBuilder::moveJog(),      QString("MoveJog()\n"));
    }

    void testArc()
    {
        CartesianPose via{100, 200, 300, 0, 0, 0};
        CartesianPose end{400, 500, 600, 0, 0, 0};
        QString cmd = CommandBuilder::arc(via, end, 60, 40, 1.0);
        QVERIFY(cmd.startsWith("Arc("));
        QVERIFY(cmd.contains("SpeedL=60"));
        QVERIFY(cmd.contains("AccL=40"));
    }

    void testGetCurrentCommandID()
    {
        QCOMPARE(CommandBuilder::getCurrentCommandID(),
                 QString("GetCurrentCommandID()\n"));
    }

    void testServoP()
    {
        CartesianPose p{100.0, 50.0, 30.0, 10.0, 20.0, 30.0};
        QString cmd = CommandBuilder::servoP(p);
        QVERIFY(cmd.startsWith("ServoP("));
        QVERIFY(cmd.contains("100.0000"));
        QVERIFY(cmd.contains("0.0080")); // default t=0.008
        QVERIFY(cmd.endsWith(")\n"));
    }

    void testPowerOn()
    {
        QCOMPARE(CommandBuilder::powerOn(), QString("PowerOn()\n"));
    }

    void testStop()
    {
        QCOMPARE(CommandBuilder::stop(), QString("Stop()\n"));
    }

    void testCollisionLevel()
    {
        QCOMPARE(CommandBuilder::setCollisionLevel(3), QString("SetCollisionLevel(3)\n"));
        // Clamping
        QCOMPARE(CommandBuilder::setCollisionLevel(-1), QString("SetCollisionLevel(0)\n"));
        QCOMPARE(CommandBuilder::setCollisionLevel(10), QString("SetCollisionLevel(5)\n"));
    }
};

QTEST_APPLESS_MAIN(TestCommandBuilder)
#include "test_command_builder.moc"
