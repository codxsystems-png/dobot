// ═══════════════════════════════════════════════════════════════════════════════
// Test: AxisBoardLink — one serial port shared between several axes.
// Covers the reply routing, the version handshake (including the Uno's
// DTR-reset silence), and protocol-mismatch refusal.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "infrastructure/gantry/axis_board_link.h"
#include "infrastructure/gantry/fake_serial_transport.h"

namespace {

/// Records what it was handed, so a test can assert on routing.
class RecordingHandler : public IAxisReplyHandler
{
public:
    QList<axisproto::Reply> replies;
    int linkLostCount = 0;

    void onReply(const axisproto::Reply& r) override { replies.append(r); }
    void onLinkLost() override { ++linkLostCount; }

    bool sawType(char t) const {
        for (const auto& r : replies) if (r.type == t) return true;
        return false;
    }
};

const QString kGoodVersion = "V FW=4 PROTO=3 BOARD=UNO AXES=3 CAPS=STEP";

/// Opens the link and completes the handshake.
void bringUp(AxisBoardLink& link, FakeSerialTransport* fake)
{
    link.connectPort("COM_FAKE");
    fake->pushIncomingLine(kGoodVersion);
    fake->emitReadyRead();
}

} // namespace

class TestAxisBoardLink : public QObject
{
    Q_OBJECT
private slots:
    // ─── Handshake ────────────────────────────────────────────────────────

    void testOpenAloneIsNotEnoughToBeUsable()
    {
        // Opening the port asserts DTR, which reboots an Uno — the sketch is
        // not running for ~1.6s. Treating "port open" as "ready" is exactly
        // how every connect would silently fail.
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);

        QVERIFY(link.connectPort("COM_FAKE"));
        QVERIFY(link.isConnected());
        QVERIFY2(!link.isIdentified(), "must not be usable before the board answers");
    }

    void testHandshakeProbesImmediately()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        link.connectPort("COM_FAKE");

        QVERIFY2(fake->wasCommandSent("V"),
                 "a version probe should go out as soon as the port opens");
    }

    void testIdentificationOnGoodVersion()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        QSignalSpy idSpy(&link, &AxisBoardLink::identified);

        bringUp(link, fake);

        QVERIFY(link.isIdentified());
        QCOMPARE(idSpy.count(), 1);
        QCOMPARE(link.versionInfo().firmware, 4);
        QCOMPARE(link.versionInfo().board, QStringLiteral("UNO"));
        QCOMPARE(link.versionInfo().axisCount, 3);
    }

    void testProtocolMismatchIsRefused()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        QSignalSpy failSpy(&link, &AxisBoardLink::identificationFailed);

        link.connectPort("COM_FAKE");
        fake->pushIncomingLine("V FW=9 PROTO=4 BOARD=UNO AXES=4 CAPS=STEP");
        fake->emitReadyRead();

        QVERIFY2(!link.isIdentified(), "a different protocol major must not be accepted");
        QCOMPARE(failSpy.count(), 1);
        QVERIFY(failSpy.at(0).at(0).toString().contains("protocol"));
    }

    void testHandshakeRetriesWhileBoardIsBooting()
    {
        // Simulates the bootloader window: silence, then an answer.
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        link.connectPort("COM_FAKE");
        fake->clearWrittenCommands();

        // Let a couple of retry ticks elapse with no reply.
        QTest::qWait(600);
        QVERIFY2(fake->commandsMatching("V").size() >= 1,
                 "should keep probing while the board is still booting");
        QVERIFY(!link.isIdentified());

        // Board finally comes up.
        fake->pushIncomingLine(kGoodVersion);
        fake->emitReadyRead();
        QVERIFY(link.isIdentified());
    }

    void testHandshakeStopsProbingOnceIdentified()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        bringUp(link, fake);
        fake->clearWrittenCommands();

        QTest::qWait(600);
        QCOMPARE(fake->commandsMatching("V").size(), 0);
    }

    // ─── Routing ──────────────────────────────────────────────────────────

    void testRepliesGoToTheirOwnAxisOnly()
    {
        // The reason this class exists: two axes on one port must not see
        // each other's replies.
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        RecordingHandler axis0, axis1;
        link.registerAxis(0, &axis0);
        link.registerAxis(1, &axis1);

        bringUp(link, fake);

        fake->pushIncomingLine("Q 0 5000");
        fake->pushIncomingLine("Q 1 -250");
        fake->emitReadyRead();

        QCOMPARE(axis0.replies.size(), 1);
        QCOMPARE(axis1.replies.size(), 1);
        QCOMPARE(axis0.replies.at(0).args.at(0).toLong(), 5000L);
        QCOMPARE(axis1.replies.at(0).args.at(0).toLong(), -250L);
    }

    void testGlobalRepliesAreNotRoutedToAxes()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        RecordingHandler axis0;
        link.registerAxis(0, &axis0);

        bringUp(link, fake);              // the V reply itself is global
        fake->pushIncomingLine("P OK");
        fake->emitReadyRead();

        QVERIFY2(!axis0.sawType('V'), "version is the link's business, not an axis's");
        QVERIFY2(!axis0.sawType('P'), "ping is global");
    }

    void testUnsolicitedFaultReachesItsAxisImmediately()
    {
        // Alarms arrive whenever the board feels like it, not in reply to
        // anything. Under v1 this line would have been mis-read as the answer
        // to whatever query was outstanding.
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        RecordingHandler axis0, axis1;
        link.registerAxis(0, &axis0);
        link.registerAxis(1, &axis1);
        bringUp(link, fake);

        fake->pushIncomingLine("! 1 1 alarm");
        fake->emitReadyRead();

        QCOMPARE(axis1.replies.size(), 1);
        QCOMPARE(axis1.replies.at(0).type, '!');
        QCOMPARE(axis0.replies.size(), 0);
    }

    void testInterleavedRepliesAreNotMisattributed()
    {
        // The v1 regression, directly: a fault landing between a query and
        // its answer must not be consumed as that answer.
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        RecordingHandler axis0;
        link.registerAxis(0, &axis0);
        bringUp(link, fake);

        fake->pushIncomingLine("! 0 3 limit");
        fake->pushIncomingLine("Q 0 1234");
        fake->emitReadyRead();

        QCOMPARE(axis0.replies.size(), 2);
        QCOMPARE(axis0.replies.at(0).type, '!');
        QCOMPARE(axis0.replies.at(1).type, 'Q');
        QCOMPARE(axis0.replies.at(1).args.at(0).toLong(), 1234L);
    }

    void testGarbageAndCommentsAreDiscarded()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        RecordingHandler axis0;
        link.registerAxis(0, &axis0);
        bringUp(link, fake);

        fake->pushIncomingLine("# CamBot axis board v2 ready");
        fake->pushIncomingLine("5000");        // a bare v1-style value
        fake->pushIncomingLine("total noise");
        fake->emitReadyRead();

        QCOMPARE(axis0.replies.size(), 0);
    }

    void testRepliesForUnregisteredAxisAreDropped()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        RecordingHandler axis0;
        link.registerAxis(0, &axis0);
        bringUp(link, fake);

        fake->pushIncomingLine("Q 7 999");     // no such axis registered
        fake->emitReadyRead();

        QCOMPARE(axis0.replies.size(), 0);     // and definitely not to axis 0
    }

    void testPartialLinesAreBufferedUntilComplete()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        RecordingHandler axis0;
        link.registerAxis(0, &axis0);
        bringUp(link, fake);

        // Serial delivers whatever arrived; a reply can straddle two reads.
        fake->pushIncomingLine("Q 0 4242");
        fake->emitReadyRead();
        QCOMPARE(axis0.replies.size(), 1);
        QCOMPARE(axis0.replies.at(0).args.at(0).toLong(), 4242L);
    }

    // ─── Link loss ────────────────────────────────────────────────────────

    void testFatalErrorNotifiesEveryAxisAndSchedulesReconnect()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        RecordingHandler axis0, axis1;
        link.registerAxis(0, &axis0);
        link.registerAxis(1, &axis1);
        bringUp(link, fake);

        fake->emitFatalError("device disappeared");

        QVERIFY(!link.isConnected());
        QVERIFY2(!link.isIdentified(), "identification must not survive a link drop");
        QCOMPARE(axis0.linkLostCount, 1);
        QCOMPARE(axis1.linkLostCount, 1);
        QCOMPARE(link.connectionState(), ConnectionStateMachine::State::Reconnecting);
    }

    void testDisconnectNotifiesAxes()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        RecordingHandler axis0;
        link.registerAxis(0, &axis0);
        bringUp(link, fake);

        link.disconnectPort();

        QVERIFY(!link.isConnected());
        QCOMPARE(axis0.linkLostCount, 1);
    }

    void testOpenFailurePropagates()
    {
        auto* fake = new FakeSerialTransport();
        fake->failNextOpen();
        AxisBoardLink link(fake);
        QSignalSpy errSpy(&link, &AxisBoardLink::errorOccurred);

        QVERIFY(!link.connectPort("COM_FAKE"));
        QVERIFY(!link.isConnected());
        QVERIFY(errSpy.count() >= 1);
    }

    void testSendIsIgnoredWhenClosed()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);

        link.send(axisproto::cmdTarget(0, 100));
        QCOMPARE(fake->writtenCommands().size(), 0);
    }

    /// The boot banner arriving mid-session means the board reset without
    /// being asked. It must be reported: every step count on the board is now
    /// zero while the host still believes its positions are valid, and there
    /// is no reconnection to re-run the handshake and reveal it.
    void testUnexpectedBoardResetIsDetected()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        QSignalSpy resetSpy(&link, &AxisBoardLink::boardReset);

        link.connectPort("COM_FAKE");
        fake->pushIncomingLine(kGoodVersion);
        fake->emitReadyRead();
        QVERIFY(link.isIdentified());

        fake->pushIncomingLine("# CamBot axis board v3 (FW4, 3x STEP) ready");
        fake->emitReadyRead();

        QCOMPARE(resetSpy.count(), 1);
    }

    /// A reset MID-REPLY leaves a truncated stub in the buffer, so the banner
    /// arrives with garbage in front of it: "Q # CamBot ... ready". Requiring
    /// the line to start with '#' missed every real reset for exactly this
    /// reason — six in one session, all silently discarded.
    void testResetIsDetectedEvenWithAStubInFront()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        QSignalSpy resetSpy(&link, &AxisBoardLink::boardReset);

        link.connectPort("COM_FAKE");
        fake->pushIncomingLine(kGoodVersion);
        fake->emitReadyRead();

        fake->pushIncomingLine("Q # CamBot axis board v3 (FW4, 3x STEP) ready");
        fake->emitReadyRead();

        QCOMPARE(resetSpy.count(), 1);
    }

    /// The same corruption swallowed real FAULTS. A drive alarm is the only
    /// integrity signal a stepper axis has, so losing it to a stray prefix is
    /// the worst possible line to drop.
    void testFaultIsRecoveredFromACorruptedLine()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        RecordingHandler handler;
        link.registerAxis(0, &handler);

        link.connectPort("COM_FAKE");
        fake->pushIncomingLine(kGoodVersion);
        fake->emitReadyRead();
        handler.replies.clear();

        fake->pushIncomingLine("Q ! 0 1 drive alarm");
        fake->emitReadyRead();

        QCOMPARE(handler.replies.size(), 1);
        QCOMPARE(handler.replies.first().type, '!');
    }

    /// The banner seen BEFORE identification is just the normal boot message
    /// from opening the port, and must not be reported as a fault.
    void testBannerBeforeIdentificationIsNotAReset()
    {
        auto* fake = new FakeSerialTransport();
        AxisBoardLink link(fake);
        QSignalSpy resetSpy(&link, &AxisBoardLink::boardReset);

        link.connectPort("COM_FAKE");
        fake->pushIncomingLine("# CamBot axis board v3 (FW4, 3x STEP) ready");
        fake->emitReadyRead();

        QCOMPARE(resetSpy.count(), 0);
    }
};

QTEST_MAIN(TestAxisBoardLink)
#include "test_axis_board_link.moc"
