// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Axis Board Link
// ═══════════════════════════════════════════════════════════════════════════════

#include "infrastructure/gantry/axis_board_link.h"
#include "infrastructure/gantry/qt_serial_transport.h"
#include "core/structured_logger.h"
#include <QDebug>

#ifdef HAS_SERIALPORT
#include <QSerialPortInfo>
#endif

AxisBoardLink::AxisBoardLink(ISerialTransport* transport, QObject* parent)
    : QObject(parent)
{
    m_transport = transport ? transport : createDefaultSerialTransport(this);
    if (m_transport) {
        m_transport->setParent(this);
        connect(m_transport, &ISerialTransport::readyRead,
                this, &AxisBoardLink::onReadyRead);
        connect(m_transport, &ISerialTransport::errorOccurred,
                this, &AxisBoardLink::onTransportError);
    }

    m_stateMachine = new ConnectionStateMachine("AxisBoardLink", this);
    m_stateMachine->setBackoffPolicy(1000, 15000, 2.0);
    connect(m_stateMachine, &ConnectionStateMachine::reconnectRequested,
            this, &AxisBoardLink::onReconnectRequested);

    m_handshakeTimer = new QTimer(this);
    m_handshakeTimer->setInterval(HANDSHAKE_INTERVAL_MS);
    connect(m_handshakeTimer, &QTimer::timeout, this, &AxisBoardLink::onHandshakeTick);
}

AxisBoardLink::~AxisBoardLink()
{
    teardown();
}

QStringList AxisBoardLink::availablePorts() const
{
    QStringList ports;
#ifdef HAS_SERIALPORT
    for (const auto& info : QSerialPortInfo::availablePorts())
        ports << info.portName();
#endif
    return ports;
}

void AxisBoardLink::registerAxis(int axis, IAxisReplyHandler* handler)
{
    if (!handler) return;

    // Two handlers on one address is always a wiring mistake, and it fails in
    // the worst possible way: the newcomer silently takes over the incumbent's
    // replies, so the displaced axis simply stops hearing about its own
    // encoder, limit switch and faults — with no error anywhere. Say so.
    auto existing = m_handlers.constFind(axis);
    if (existing != m_handlers.constEnd() && *existing != handler) {
        StructuredLogger::instance().log(StructuredLogger::Category::Safety,
            "AxisBoardLink",
            QString("Axis %1 already had a reply handler; the new one takes over and "
                    "the previous axis will stop receiving its replies. Two controllers "
                    "share a board address.").arg(axis));
    }
    m_handlers.insert(axis, handler);
}

void AxisBoardLink::unregisterAxis(int axis)
{
    m_handlers.remove(axis);
}

void AxisBoardLink::send(const QByteArray& command)
{
    if (!m_transport || !m_transport->isOpen()) return;
    m_transport->write(command);
}

bool AxisBoardLink::connectPort(const QString& portName)
{
    m_lastPortName = portName;
    m_stateMachine->notifyConnecting();

    teardown();

    if (!m_transport) {
        QString msg = "Qt6::SerialPort not available — axis board disabled";
        emit errorOccurred(msg);
        m_stateMachine->notifyFault(msg);
        return false;
    }

    if (!m_transport->open(portName)) {
        QString msg = "Failed to open " + portName + ": " + m_transport->lastErrorString();
        emit errorOccurred(msg);
        m_stateMachine->notifyFault(msg);
        return false;
    }

    m_connected  = true;
    m_identified = false;
    m_readBuffer.clear();

    qDebug() << "AxisBoardLink: port open" << portName << "— identifying";
    emit connected(portName);
    m_stateMachine->notifyConnected();

    beginHandshake();
    return true;
}

void AxisBoardLink::disconnectPort()
{
    bool wasConnected = m_connected;
    teardown();
    if (wasConnected) {
        m_stateMachine->notifyDisconnected("user requested");
    }
}

void AxisBoardLink::teardown()
{
    m_handshakeTimer->stop();

    if (m_transport && m_transport->isOpen()) {
        m_transport->write(axisproto::cmdStopAll()); // best-effort stop before closing
        m_transport->flush();
        m_transport->close();
    }

    if (m_connected) {
        m_connected  = false;
        m_identified = false;
        // Tell every axis the link is gone, so none of them keeps trusting a
        // cached position or believing it is still referenced.
        for (auto* handler : m_handlers) {
            if (handler) handler->onLinkLost();
        }
        emit disconnected();
        qDebug() << "AxisBoardLink: disconnected";
    }
}

void AxisBoardLink::beginHandshake()
{
    m_handshakeAttempts = 0;
    // Probe immediately, then keep retrying: opening the port asserts DTR,
    // which reboots an Uno, so the first probe almost always lands while the
    // bootloader is still running.
    onHandshakeTick();
    m_handshakeTimer->start();
}

void AxisBoardLink::onHandshakeTick()
{
    if (m_identified) {
        m_handshakeTimer->stop();
        return;
    }

    if (++m_handshakeAttempts > HANDSHAKE_MAX_ATTEMPTS) {
        m_handshakeTimer->stop();
        QString reason =
            QString("Board did not identify itself within %1s. Either it is running "
                    "the old v1 firmware, the baud rate is wrong, or this is the "
                    "wrong port. Flash firmware/cambot_axis_v3.")
                .arg(HANDSHAKE_MAX_ATTEMPTS * HANDSHAKE_INTERVAL_MS / 1000);
        StructuredLogger::instance().log(StructuredLogger::Category::Connection,
            "AxisBoardLink", reason);
        emit errorOccurred(reason);
        emit identificationFailed(reason);
        return;
    }

    send(axisproto::cmdVersion());
}

void AxisBoardLink::onReadyRead()
{
    m_readBuffer.append(m_transport->readAll());

    while (m_readBuffer.contains('\n')) {
        int idx = m_readBuffer.indexOf('\n');
        QString line = QString::fromUtf8(m_readBuffer.left(idx));
        m_readBuffer.remove(0, idx + 1);

        auto reply = axisproto::parseLine(line);
        if (!reply) {
            // Comments, blanks and unknown types land here. Never guess at
            // what an unrecognised line meant — that was exactly the v1 bug.
            QString trimmed = line.trimmed();

            // The board's boot banner arriving mid-session means it RESET
            // without us asking — a brown-out, a loose USB, a supply dip.
            //
            // That has to be loud. Every step count on the board is now zero
            // while the host still believes its positions are valid, so an
            // undetected reset means the next move is computed against an
            // origin that no longer exists. The host is never told otherwise:
            // there is no reconnection, so nothing re-runs the handshake.
            if (m_identified && trimmed.contains("ready") && trimmed.startsWith('#')) {
                StructuredLogger::instance().log(StructuredLogger::Category::Safety,
                    "AxisBoardLink",
                    "THE AXIS BOARD RESET UNEXPECTEDLY. Every axis position is lost "
                    "and must be re-zeroed. This is usually a power problem — a "
                    "supply dip from the motors, or a marginal USB connection.");
                emit boardReset();
            }
            if (!trimmed.isEmpty() && !trimmed.startsWith('#')) {
                StructuredLogger::instance().log(StructuredLogger::Category::Connection,
                    "AxisBoardLink", "Discarded unrecognised line: " + trimmed);
            }
            continue;
        }
        dispatch(*reply);
    }
}

void AxisBoardLink::dispatch(const axisproto::Reply& reply)
{
    // The version reply is the link's own business, not any axis's.
    if (reply.type == 'V') {
        auto info = axisproto::parseVersion(reply);
        if (!info) {
            emit errorOccurred("Malformed version reply from axis board: " + reply.raw);
            return;
        }
        m_handshakeTimer->stop();

        if (!axisproto::isCompatible(*info)) {
            m_identified = false;
            QString reason =
                QString("Axis board speaks protocol v%1 but this build expects v%2. "
                        "Flash firmware/cambot_axis_v3 (or update the app).")
                    .arg(info->protocol).arg(axisproto::kProtocolVersion);
            StructuredLogger::instance().log(StructuredLogger::Category::Connection,
                "AxisBoardLink", reason);
            emit errorOccurred(reason);
            emit identificationFailed(reason);
            return;
        }

        m_version    = *info;
        m_identified = true;
        StructuredLogger::instance().log(StructuredLogger::Category::Connection,
            "AxisBoardLink",
            QString("Board identified: FW=%1 PROTO=%2 BOARD=%3 AXES=%4")
                .arg(info->firmware).arg(info->protocol)
                .arg(info->board).arg(info->axisCount));
        emit identified(*info);
        return;
    }

    // Ping is also global; nothing needs it beyond proving liveness.
    if (reply.type == 'P') return;

    // Everything else belongs to an axis.
    if (reply.axis < 0) return;
    auto it = m_handlers.constFind(reply.axis);
    if (it == m_handlers.constEnd() || !*it) {
        StructuredLogger::instance().log(StructuredLogger::Category::Connection,
            "AxisBoardLink",
            QString("Reply for unregistered axis %1: %2").arg(reply.axis).arg(reply.raw));
        return;
    }
    (*it)->onReply(reply);
}

void AxisBoardLink::onTransportError(const QString& message, bool isFatal)
{
    QString msg = "Serial error: " + message;
    StructuredLogger::instance().log(StructuredLogger::Category::Connection,
        "AxisBoardLink", msg);
    emit errorOccurred(msg);

    if (isFatal) {
        // Device went away (unplugged, driver reset). A fault, not a
        // user-requested disconnect, so schedule a backed-off reconnect.
        teardown();
        m_stateMachine->notifyFault(msg);
    }
}

void AxisBoardLink::onReconnectRequested()
{
    qDebug() << "AxisBoardLink: attempting reconnect to" << m_lastPortName;
    connectPort(m_lastPortName);
}
