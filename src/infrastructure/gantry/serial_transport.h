#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Serial Transport Interface (Phase 7: testability seam)
// Abstracts the gantry's serial link so GantryAxisController's protocol
// logic (homing, closed-loop tick, half-duplex query tracking) can be unit
// tested with a fake transport — no real QSerialPort or hardware needed.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QByteArray>
#include <QString>

class ISerialTransport : public QObject
{
    Q_OBJECT
public:
    explicit ISerialTransport(QObject* parent = nullptr) : QObject(parent) {}
    ~ISerialTransport() override = default;

    virtual bool open(const QString& portName) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual void write(const QByteArray& data) = 0;
    virtual void flush() = 0;
    virtual QByteArray readAll() = 0;
    virtual QString lastErrorString() const = 0;

signals:
    void readyRead();

    /// isFatal=true means the device itself is gone (unplugged, driver
    /// reset, ...) — GantryAxisController tears down and schedules an
    /// auto-reconnect for that case. Other transport errors are surfaced
    /// but treated as non-fatal.
    void errorOccurred(const QString& message, bool isFatal);
};
