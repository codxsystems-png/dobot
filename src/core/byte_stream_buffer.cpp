// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Byte Stream Buffer
// ═══════════════════════════════════════════════════════════════════════════════

#include "core/byte_stream_buffer.h"

ByteStreamBuffer::ByteStreamBuffer(int frameSize)
    : m_frameSize(frameSize)
{
    m_buffer.reserve(frameSize * 4); // pre-allocate for ~4 frames
}

void ByteStreamBuffer::append(const QByteArray& data)
{
    m_buffer.append(data);
}

void ByteStreamBuffer::append(const char* data, int length)
{
    m_buffer.append(data, length);
}

QList<QByteArray> ByteStreamBuffer::extractFrames()
{
    QList<QByteArray> frames;

    while (m_buffer.size() >= m_frameSize) {
        frames.append(m_buffer.left(m_frameSize));
        m_buffer.remove(0, m_frameSize);
    }

    return frames;
}

bool ByteStreamBuffer::hasFrame() const
{
    return m_buffer.size() >= m_frameSize;
}

QByteArray ByteStreamBuffer::extractOneFrame()
{
    if (m_buffer.size() < m_frameSize)
        return {};

    QByteArray frame = m_buffer.left(m_frameSize);
    m_buffer.remove(0, m_frameSize);
    return frame;
}

int ByteStreamBuffer::pendingBytes() const
{
    return m_buffer.size();
}

void ByteStreamBuffer::clear()
{
    m_buffer.clear();
}
