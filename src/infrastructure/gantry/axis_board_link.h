#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Axis Board Link
//
// Owns the serial connection to one Arduino axis board and multiplexes it
// between the controllers for the axes on that board.
//
// This exists because a CL57C stepper and the DC servo share one Arduino,
// therefore one QSerialPort, therefore one ISerialTransport — but they need
// independent control paths. The previous shape (each controller owning its
// own transport) cannot express that.
//
// Responsibilities: open/close/reconnect, the version handshake, line framing,
// and dispatching each parsed reply to the controller registered for its axis.
// Controllers never touch the transport directly.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QTimer>
#include <QStringList>
#include "core/axis_protocol.h"
#include "infrastructure/gantry/serial_transport.h"
#include "services/connection_state_machine.h"

/// Anything that can receive replies for one axis on a board.
class IAxisReplyHandler
{
public:
    virtual ~IAxisReplyHandler() = default;
    virtual void onReply(const axisproto::Reply& reply) = 0;
    /// Called when the link goes down, so the axis can drop its homed state
    /// and stop believing its cached position.
    virtual void onLinkLost() = 0;
};

class AxisBoardLink : public QObject
{
    Q_OBJECT
public:
    /// transport == nullptr uses the real QSerialPort-backed transport (or a
    /// no-op one when Qt6::SerialPort isn't available). Tests pass a
    /// FakeSerialTransport.
    explicit AxisBoardLink(ISerialTransport* transport = nullptr, QObject* parent = nullptr);
    ~AxisBoardLink() override;

    QStringList availablePorts() const;

    /// Port is open. NOT the same as usable — see isIdentified().
    bool isConnected() const { return m_connected; }

    /// The board answered the version handshake with a compatible protocol.
    /// Motion commands must be gated on this, not merely on isConnected():
    /// opening the port asserts DTR, which reboots an Uno, leaving roughly
    /// 1.6s where the sketch simply isn't running.
    bool isIdentified() const { return m_identified; }

    axisproto::VersionInfo versionInfo() const { return m_version; }

    ConnectionStateMachine::State connectionState() const { return m_stateMachine->state(); }

    /// Routes replies for `axis` to `handler`. The handler must outlive the
    /// registration or call unregisterAxis().
    void registerAxis(int axis, IAxisReplyHandler* handler);
    void unregisterAxis(int axis);

    /// Queues a command line for the board. No-op when the port is closed.
    void send(const QByteArray& command);

public slots:
    bool connectPort(const QString& portName);
    void disconnectPort();

signals:
    void connected(const QString& portName);
    void disconnected();
    /// Handshake completed and the protocol matched — the board is usable.
    void identified(const axisproto::VersionInfo& info);
    /// Handshake failed: timed out, or the board speaks a different protocol.
    void identificationFailed(const QString& reason);
    void errorOccurred(const QString& message);

private slots:
    void onReadyRead();
    void onTransportError(const QString& message, bool isFatal);
    void onReconnectRequested();
    void onHandshakeTick();

private:
    void teardown();
    void beginHandshake();
    void dispatch(const axisproto::Reply& reply);

    ISerialTransport*       m_transport    = nullptr;
    ConnectionStateMachine* m_stateMachine = nullptr;
    QString                 m_lastPortName;

    bool m_connected  = false;
    bool m_identified = false;
    axisproto::VersionInfo m_version;

    // Handshake retry. Opening the port resets an Uno, so a single probe
    // would always fail; retry until the bootloader has handed over.
    QTimer* m_handshakeTimer = nullptr;
    int     m_handshakeAttempts = 0;
    static constexpr int HANDSHAKE_INTERVAL_MS = 250;
    static constexpr int HANDSHAKE_MAX_ATTEMPTS = 12;   // ~3s

    QHash<int, IAxisReplyHandler*> m_handlers;
    QByteArray m_readBuffer;
};
