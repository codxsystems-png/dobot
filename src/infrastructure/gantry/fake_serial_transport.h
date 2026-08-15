#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Fake Serial Transport (Phase 7: test-only)
// In-memory stand-in for the gantry's Arduino link: the test scripts what
// GantryAxisController "reads back" and inspects what it wrote, without any
// real serial port or hardware.
// ═══════════════════════════════════════════════════════════════════════════════

#include "infrastructure/gantry/serial_transport.h"
#include <QList>

class FakeSerialTransport : public ISerialTransport
{
    Q_OBJECT
public:
    explicit FakeSerialTransport(QObject* parent = nullptr) : ISerialTransport(parent) {}

    bool open(const QString& /*portName*/) override {
        if (m_failNextOpen) {
            m_failNextOpen = false;
            m_lastErrorString = "simulated open failure";
            return false;
        }
        m_open = true;
        return true;
    }

    void close() override { m_open = false; }
    bool isOpen() const override { return m_open; }

    void write(const QByteArray& data) override {
        m_writtenCommands.append(QString::fromUtf8(data).trimmed());
    }

    void flush() override {}

    QByteArray readAll() override {
        QByteArray data = m_pendingRead;
        m_pendingRead.clear();
        return data;
    }

    QString lastErrorString() const override { return m_lastErrorString; }

    // ── Test-side controls ──────────────────────────────────────────────

    /// Queue a line (without trailing '\n') to be returned by the next
    /// readAll() after emitReadyRead() is called.
    void pushIncomingLine(const QString& line) {
        m_pendingRead.append(line.toUtf8());
        m_pendingRead.append('\n');
    }

    void emitReadyRead() { emit readyRead(); }

    void emitFatalError(const QString& message) {
        m_lastErrorString = message;
        emit errorOccurred(message, /*isFatal=*/true);
    }

    void failNextOpen() { m_failNextOpen = true; }

    QStringList writtenCommands() const { return m_writtenCommands; }
    void clearWrittenCommands() { m_writtenCommands.clear(); }

    /// Convenience: has a "g <pwm>" command been sent with this exact value?
    bool wasPwmCommandSent(int pwm) const {
        return m_writtenCommands.contains(QString("g %1").arg(pwm));
    }

private:
    bool m_open = false;
    bool m_failNextOpen = false;
    QByteArray m_pendingRead;
    QString m_lastErrorString;
    QStringList m_writtenCommands;
};
