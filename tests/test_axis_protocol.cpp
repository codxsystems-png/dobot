// ═══════════════════════════════════════════════════════════════════════════════
// Test: axisproto — the v2 axis-board wire format.
// Pure string handling, no Qt widgets, no serial, no hardware.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "core/axis_protocol.h"

using namespace axisproto;

class TestAxisProtocol : public QObject
{
    Q_OBJECT
private slots:
    // ─── Commands ─────────────────────────────────────────────────────────

    void testCommandsAreNewlineTerminated()
    {
        // The transport writes these verbatim; a missing terminator would
        // leave the firmware's line parser waiting forever.
        for (const QByteArray& c : { cmdVersion(), cmdEnumerate(), cmdPing(),
                                      cmdStopAll(),
                                      cmdTarget(1, 5000), cmdQuery(0) }) {
            QVERIFY2(c.endsWith('\n'), qPrintable("not terminated: " + c));
        }
    }

    void testCommandWireFormat()
    {
        QCOMPARE(cmdVersion(),            QByteArray("V\n"));
        QCOMPARE(cmdEnumerate(),          QByteArray("A\n"));
        QCOMPARE(cmdPing(),               QByteArray("P\n"));
        QCOMPARE(cmdStopAll(),            QByteArray("X\n"));
        QCOMPARE(cmdTarget(1, 123456),    QByteArray("T 1 123456\n"));
        QCOMPARE(cmdTarget(1, -500),      QByteArray("T 1 -500\n"));
        QCOMPARE(cmdJog(1, -800),         QByteArray("J 1 -800\n"));
        QCOMPARE(cmdLimits(1, 8000, 4000),QByteArray("L 1 8000 4000\n"));
        QCOMPARE(cmdEnable(1, true),      QByteArray("E 1 1\n"));
        QCOMPARE(cmdEnable(1, false),     QByteArray("E 1 0\n"));
        QCOMPARE(cmdQuery(0),             QByteArray("Q 0\n"));
        QCOMPARE(cmdZero(1),              QByteArray("Z 1\n"));
        QCOMPARE(cmdHome(0),              QByteArray("H 0\n"));
        QCOMPARE(cmdStatus(1),            QByteArray("S 1\n"));
        QCOMPARE(cmdResume(1),            QByteArray("R 1\n"));
    }

    // ─── Reply parsing ────────────────────────────────────────────────────

    void testParsePositionReply()
    {
        auto r = parseLine("Q 0 5000");
        QVERIFY(r.has_value());
        QCOMPARE(r->type, 'Q');
        QCOMPARE(r->axis, 0);
        QCOMPARE(r->args.size(), 1);
        QCOMPARE(r->args.at(0).toLong(), 5000L);
    }

    void testParseNegativePosition()
    {
        auto r = parseLine("Q 1 -12345");
        QVERIFY(r.has_value());
        QCOMPARE(r->axis, 1);
        QCOMPARE(r->args.at(0).toLong(), -12345L);
    }

    void testParseHomeSwitchReply()
    {
        auto r = parseLine("H 0 1");
        QVERIFY(r.has_value());
        QCOMPARE(r->type, 'H');
        QCOMPARE(r->axis, 0);
        QCOMPARE(r->args.at(0), QStringLiteral("1"));
    }

    void testParseFaultReplyKeepsMessageText()
    {
        auto r = parseLine("! 1 1 alarm");
        QVERIFY(r.has_value());
        QCOMPARE(r->type, '!');
        QCOMPARE(r->axis, 1);
        QCOMPARE(r->args.at(0).toInt(), static_cast<int>(FaultAlarm));
        QCOMPARE(r->args.at(1), QStringLiteral("alarm"));
    }

    void testParseHandlesExtraWhitespace()
    {
        // Serial framing can leave ragged spacing; it must not change meaning.
        auto r = parseLine("  Q   0    5000  \r");
        QVERIFY(r.has_value());
        QCOMPARE(r->axis, 0);
        QCOMPARE(r->args.at(0).toLong(), 5000L);
    }

    // ─── Lines that must be ignored, not guessed at ───────────────────────

    void testBlankAndCommentLinesAreIgnored()
    {
        QVERIFY(!parseLine("").has_value());
        QVERIFY(!parseLine("   ").has_value());
        QVERIFY(!parseLine("# booting").has_value());
        QVERIFY(!parseLine("  # indented comment").has_value());
    }

    void testUnknownTypesAreIgnoredNotMisattributed()
    {
        // This is the whole point of the redesign: v1 matched replies
        // positionally against the pending query, so a stray line became
        // whatever the host happened to be waiting for. Unknown types must
        // produce nothing at all.
        QVERIFY(!parseLine("W 0 123").has_value());
        QVERIFY(!parseLine("garbage").has_value());
        QVERIFY(!parseLine("5000").has_value());   // a bare v1-style value
        QVERIFY(!parseLine("1").has_value());      // a bare v1-style home flag
    }

    void testAxisBearingRepliesRequireAnAxis()
    {
        QVERIFY(!parseLine("Q").has_value());
        QVERIFY(!parseLine("Q notanumber 5000").has_value());
        QVERIFY(!parseLine("Q -1 5000").has_value());
    }

    void testGlobalRepliesCarryNoAxis()
    {
        auto p = parseLine("P OK");
        QVERIFY(p.has_value());
        QCOMPARE(p->type, 'P');
        QCOMPARE(p->axis, -1);
    }

    // ─── Version handshake ────────────────────────────────────────────────

    void testParseVersionReply()
    {
        auto r = parseLine("V FW=4 PROTO=3 BOARD=UNO AXES=3 CAPS=STEP");
        QVERIFY(r.has_value());

        auto v = parseVersion(*r);
        QVERIFY(v.has_value());
        QCOMPARE(v->firmware, 4);
        QCOMPARE(v->protocol, 3);
        QCOMPARE(v->board, QStringLiteral("UNO"));
        QCOMPARE(v->axisCount, 3);
        QCOMPARE(v->caps.size(), 1);
        QVERIFY(v->caps.contains("STEP"));
        QVERIFY(isCompatible(*v));
    }

    void testVersionFieldOrderDoesNotMatter()
    {
        auto r = parseLine("V BOARD=MEGA CAPS=STEP,STEP,STEP PROTO=3 AXES=3 FW=7");
        QVERIFY(r.has_value());
        auto v = parseVersion(*r);
        QVERIFY(v.has_value());
        QCOMPARE(v->firmware, 7);
        QCOMPARE(v->board, QStringLiteral("MEGA"));
        QCOMPARE(v->axisCount, 3);
    }

    void testVersionWithoutFwOrProtoIsRejected()
    {
        // A partial identification must not be treated as a valid handshake.
        auto r1 = parseLine("V BOARD=UNO AXES=2");
        QVERIFY(r1.has_value());
        QVERIFY(!parseVersion(*r1).has_value());

        auto r2 = parseLine("V FW=4 BOARD=UNO");
        QVERIFY(r2.has_value());
        QVERIFY(!parseVersion(*r2).has_value());
    }

    void testProtocolMismatchIsDetected()
    {
        auto r = parseLine("V FW=9 PROTO=4 BOARD=UNO AXES=4 CAPS=STEP");
        QVERIFY(r.has_value());
        auto v = parseVersion(*r);
        QVERIFY(v.has_value());
        QVERIFY2(!isCompatible(*v),
                 "a future protocol major must be refused, not tolerated");
    }

    void testParseVersionRejectsNonVersionReply()
    {
        auto r = parseLine("Q 0 5000");
        QVERIFY(r.has_value());
        QVERIFY(!parseVersion(*r).has_value());
    }

    // ─── Status ───────────────────────────────────────────────────────────

    void testParseStatusFlags()
    {
        // enabled | moving | alarm = 0x01 | 0x02 | 0x08 = 0x0B
        auto r = parseLine("S 1 0B 12345 800.5");
        QVERIFY(r.has_value());

        auto s = parseStatus(*r);
        QVERIFY(s.has_value());
        QVERIFY(s->enabled());
        QVERIFY(s->moving());
        QVERIFY(s->alarm());
        QVERIFY(!s->halted());
        QVERIFY(!s->positionLost());
        QCOMPARE(s->position, 12345L);
        QVERIFY(qFuzzyCompare(s->rate, 800.5));
    }

    void testParseStatusHaltedAndPositionLost()
    {
        // halted | positionLost | watchdog = 0x10 | 0x20 | 0x40 = 0x70
        auto r = parseLine("S 0 70 -99 0");
        QVERIFY(r.has_value());
        auto s = parseStatus(*r);
        QVERIFY(s.has_value());
        QVERIFY(s->halted());
        QVERIFY(s->positionLost());
        QVERIFY(s->watchdog());
        QVERIFY(!s->enabled());
        QCOMPARE(s->position, -99L);
    }

    void testMalformedStatusIsRejected()
    {
        auto r = parseLine("S 1 0B");   // missing position and rate
        QVERIFY(r.has_value());
        QVERIFY(!parseStatus(*r).has_value());
    }

    // ─── Round trip ───────────────────────────────────────────────────────

    void testEnumerateRoundTrip()
    {
        auto r = parseLine("A 1 STEP");
        QVERIFY(r.has_value());
        QCOMPARE(r->type, 'A');
        QCOMPARE(r->axis, 1);
        QCOMPARE(r->args.at(0), QStringLiteral("STEP"));
    }
};

QTEST_APPLESS_MAIN(TestAxisProtocol)
#include "test_axis_protocol.moc"
