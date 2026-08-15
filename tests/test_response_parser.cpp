// ═══════════════════════════════════════════════════════════════════════════════
// Test: ResponseParser — verify Dobot ASCII response parsing
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "core/response_parser.h"

class TestResponseParser : public QObject
{
    Q_OBJECT
private slots:
    void testBasicResponse()
    {
        auto r = ResponseParser::parse("0,{},EnableRobot();");
        QVERIFY(r.valid);
        QCOMPARE(r.errorId, 0);
        QCOMPARE(r.commandName, QString("EnableRobot"));
        QVERIFY(r.values.isEmpty());
    }

    void testResponseWithValues()
    {
        auto r = ResponseParser::parse("0,{5},RobotMode();");
        QVERIFY(r.valid);
        QCOMPARE(r.errorId, 0);
        QCOMPARE(r.values.size(), 1);
        QCOMPARE(r.values[0], QString("5"));
        QCOMPARE(ResponseParser::extractInt(r, 0), 5);
    }

    void testResponseWithMultipleValues()
    {
        auto r = ResponseParser::parse("0,{45.0,-30.0,0.0,90.0,0.0,0.0},GetAngle();");
        QVERIFY(r.valid);
        QCOMPARE(r.errorId, 0);
        QCOMPARE(r.values.size(), 6);
        QCOMPARE(ResponseParser::extractDouble(r, 0), 45.0);
        QCOMPARE(ResponseParser::extractDouble(r, 1), -30.0);
    }

    void testErrorResponse()
    {
        auto r = ResponseParser::parse("-1,{},MovJ();");
        QVERIFY(r.valid);
        QCOMPARE(r.errorId, -1);
    }

    void testGetCurrentCommandID()
    {
        auto r = ResponseParser::parse("0,{42},getCurrentCommandID();");
        QVERIFY(r.valid);
        QCOMPARE(r.errorId, 0);
        QCOMPARE(ResponseParser::extractInt(r, 0), 42);
    }

    void testEmptyResponse()
    {
        auto r = ResponseParser::parse("");
        QVERIFY(!r.valid);
    }

    void testMalformedResponse()
    {
        auto r = ResponseParser::parse("garbage data");
        // Should not crash, may or may not parse depending on format
        Q_UNUSED(r);
    }

    void testResponseWithWhitespace()
    {
        auto r = ResponseParser::parse("  0 , { 5 } , RobotMode() ; ");
        QVERIFY(r.valid);
        QCOMPARE(r.errorId, 0);
        QCOMPARE(ResponseParser::extractInt(r, 0), 5);
    }

    void testExtractOutOfBounds()
    {
        auto r = ResponseParser::parse("0,{},EnableRobot();");
        QCOMPARE(ResponseParser::extractInt(r, 0), -1);    // no values
        QCOMPARE(ResponseParser::extractDouble(r, 0), 0.0); // no values
    }
};

QTEST_APPLESS_MAIN(TestResponseParser)
#include "test_response_parser.moc"
