// ═══════════════════════════════════════════════════════════════════════════════
// Test: ByteStreamBuffer — verify TCP packet reassembly
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "core/byte_stream_buffer.h"

class TestByteStreamBuffer : public QObject
{
    Q_OBJECT
private slots:
    void testEmptyBuffer()
    {
        ByteStreamBuffer buf(1440);
        QVERIFY(!buf.hasFrame());
        QCOMPARE(buf.pendingBytes(), 0);
        QVERIFY(buf.extractOneFrame().isEmpty());
    }

    void testExactFrame()
    {
        ByteStreamBuffer buf(100);
        QByteArray data(100, 'A');
        buf.append(data);

        QVERIFY(buf.hasFrame());
        QCOMPARE(buf.pendingBytes(), 100);

        QByteArray frame = buf.extractOneFrame();
        QCOMPARE(frame.size(), 100);
        QVERIFY(!buf.hasFrame());
        QCOMPARE(buf.pendingBytes(), 0);
    }

    void testPartialFrame()
    {
        ByteStreamBuffer buf(1440);

        // Append 500 bytes — not enough for a frame
        buf.append(QByteArray(500, 'B'));
        QVERIFY(!buf.hasFrame());
        QCOMPARE(buf.pendingBytes(), 500);

        // Append 500 more — still not enough
        buf.append(QByteArray(500, 'C'));
        QVERIFY(!buf.hasFrame());
        QCOMPARE(buf.pendingBytes(), 1000);

        // Append 440 more — now exactly 1 frame
        buf.append(QByteArray(440, 'D'));
        QVERIFY(buf.hasFrame());
        QCOMPARE(buf.pendingBytes(), 1440);

        QByteArray frame = buf.extractOneFrame();
        QCOMPARE(frame.size(), 1440);
        QVERIFY(!buf.hasFrame());
    }

    void testMultipleFrames()
    {
        ByteStreamBuffer buf(100);

        // Append 350 bytes — should yield 3 frames with 50 left over
        buf.append(QByteArray(350, 'E'));

        auto frames = buf.extractFrames();
        QCOMPARE(frames.size(), 3);
        for (auto& f : frames)
            QCOMPARE(f.size(), 100);

        QCOMPARE(buf.pendingBytes(), 50);
        QVERIFY(!buf.hasFrame());
    }

    void testSplitAcrossAppends()
    {
        ByteStreamBuffer buf(10);

        // Append in 3-byte chunks (simulating TCP fragmentation)
        for (int i = 0; i < 10; ++i)
            buf.append(QByteArray(3, static_cast<char>('0' + i)));

        // 30 bytes total → 3 frames of 10
        auto frames = buf.extractFrames();
        QCOMPARE(frames.size(), 3);
        QCOMPARE(buf.pendingBytes(), 0);
    }

    void testClear()
    {
        ByteStreamBuffer buf(100);
        buf.append(QByteArray(50, 'F'));
        QCOMPARE(buf.pendingBytes(), 50);

        buf.clear();
        QCOMPARE(buf.pendingBytes(), 0);
        QVERIFY(!buf.hasFrame());
    }

    void testLargeData()
    {
        ByteStreamBuffer buf(1440);

        // Simulate receiving 10 complete packets in one burst
        QByteArray bigData(1440 * 10, '\0');
        buf.append(bigData);

        auto frames = buf.extractFrames();
        QCOMPARE(frames.size(), 10);
        QCOMPARE(buf.pendingBytes(), 0);
    }

    void testFrameSize()
    {
        ByteStreamBuffer buf(1440);
        QCOMPARE(buf.frameSize(), 1440);

        ByteStreamBuffer buf2(100);
        QCOMPARE(buf2.frameSize(), 100);
    }

    void testDataIntegrity()
    {
        ByteStreamBuffer buf(4);

        // Write specific bytes across multiple appends
        buf.append(QByteArray("\x01\x02", 2));
        buf.append(QByteArray("\x03\x04", 2));

        QByteArray frame = buf.extractOneFrame();
        QCOMPARE(frame.size(), 4);
        QCOMPARE(static_cast<uint8_t>(frame[0]), 0x01u);
        QCOMPARE(static_cast<uint8_t>(frame[1]), 0x02u);
        QCOMPARE(static_cast<uint8_t>(frame[2]), 0x03u);
        QCOMPARE(static_cast<uint8_t>(frame[3]), 0x04u);
    }
};

QTEST_APPLESS_MAIN(TestByteStreamBuffer)
#include "test_byte_stream_buffer.moc"
