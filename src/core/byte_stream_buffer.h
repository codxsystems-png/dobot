#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Byte Stream Buffer for TCP Packet Reassembly
// Accumulates incoming TCP bytes and extracts complete fixed-size frames.
// NEVER assume data.size() == expectedSize — TCP doesn't guarantee alignment.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QByteArray>
#include <QList>

class ByteStreamBuffer
{
public:
    /// Construct a buffer that extracts frames of the given size.
    explicit ByteStreamBuffer(int frameSize = 1440);

    /// Append incoming bytes from TCP socket.
    void append(const QByteArray& data);

    /// Append raw bytes.
    void append(const char* data, int length);

    /// Extract all complete frames accumulated so far.
    /// Returns a list of QByteArrays, each exactly frameSize bytes.
    /// Consumed bytes are removed from the buffer.
    QList<QByteArray> extractFrames();

    /// Check if at least one complete frame is available.
    bool hasFrame() const;

    /// Extract a single frame. Returns empty QByteArray if none available.
    QByteArray extractOneFrame();

    /// Get current buffer size (pending bytes).
    int pendingBytes() const;

    /// Clear all buffered data (e.g., on reconnect).
    void clear();

    /// Get the expected frame size.
    int frameSize() const { return m_frameSize; }

private:
    QByteArray m_buffer;
    int        m_frameSize;
};
