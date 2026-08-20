// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Axis Controller Base
// ═══════════════════════════════════════════════════════════════════════════════

#include "infrastructure/gantry/axis_controller_base.h"
#include "core/structured_logger.h"
#include <QDebug>
#include <algorithm>

AxisControllerBase::AxisControllerBase(AxisBoardLink* link, int axisIndex, QObject* parent)
    : QObject(parent)
    , m_link(link)
    , m_ownsLink(false)
    , m_axisIndex(axisIndex)
{
    wireLink();
}

AxisControllerBase::AxisControllerBase(ISerialTransport* transport, int axisIndex, QObject* parent)
    : QObject(parent)
    , m_link(new AxisBoardLink(transport, this))
    , m_ownsLink(true)
    , m_axisIndex(axisIndex)
{
    wireLink();
}

AxisControllerBase::~AxisControllerBase()
{
    if (m_link) m_link->unregisterAxis(m_axisIndex);
    if (m_controlTimer) m_controlTimer->stop();
}

void AxisControllerBase::wireLink()
{
    if (!m_link) return;

    m_link->registerAxis(m_axisIndex, this);

    // Relay the link's connection signals, so consumers (MainWindow,
    // TeachPanel, the tuning dialog) talk to the axis and never to the link.
    connect(m_link, &AxisBoardLink::connected,     this, &AxisControllerBase::connected);
    connect(m_link, &AxisBoardLink::disconnected,  this, &AxisControllerBase::disconnected);
    connect(m_link, &AxisBoardLink::errorOccurred, this, &AxisControllerBase::errorOccurred);

    // The control loop is created HERE, with the link, not in connectPort().
    //
    // Several controllers share one link, and only one of them ever has
    // connectPort() called on it — the UI opens the port once. Creating the
    // timer there left every other axis on the board without one, so it never
    // ran heartbeat(), never polled its position, and never refreshed its
    // setpoint against the board's watchdog. The axis looked connected and
    // did nothing.
    m_controlTimer = new QTimer(this);
    connect(m_controlTimer, &QTimer::timeout, this, [this]() { heartbeat(); });

    // The board reboots when the port opens, so the control loop waits until
    // it has actually identified itself rather than polling a bootloader.
    // Every controller on the link sees this, which is also the right moment
    // for each to drop state that did not survive the reboot.
    connect(m_link, &AxisBoardLink::identified, this, [this](const axisproto::VersionInfo&) {
        m_isHomed = false;
        resetControlState();
        onIdentified();
        if (m_active) m_controlTimer->start(20);   // 50Hz
    });

    // An axis added while the board is ALREADY connected missed that signal
    // and will never see it again — it fires once per identification, and the
    // operator adds axes from the setup dialog with the link live.
    //
    // Without this the new axis has no control loop at all: nothing re-sends
    // its jog, so the board's watchdog stops it after 500ms ("moves once, then
    // stops"), and nothing polls its position, so the readout stays blank.
    if (m_link->isIdentified()) {
        onIdentified();
        if (m_active) m_controlTimer->start(20);
    }
}

QStringList AxisControllerBase::availablePorts() const
{
    return m_link ? m_link->availablePorts() : QStringList{};
}

bool AxisControllerBase::isConnected() const
{
    return m_link && m_link->isConnected();
}

bool AxisControllerBase::isIdentified() const
{
    return m_link && m_link->isIdentified();
}

ConnectionStateMachine::State AxisControllerBase::connectionState() const
{
    return m_link ? m_link->connectionState() : ConnectionStateMachine::State::Disconnected;
}

void AxisControllerBase::setEncoderCountsPerMm(double countsPerUnit)
{
    m_countsPerMm = qMax(0.1, countsPerUnit);
}

bool AxisControllerBase::connectPort(const QString& portName)
{
    if (!m_link) return false;

    m_isHomed = false;
    resetControlState();

    // The loop is not started here — see wireLink(): it starts once the board
    // has identified itself, for every axis on the link rather than only this
    // one.
    return m_link->connectPort(portName);
}

void AxisControllerBase::disconnectPort()
{
    if (m_link) m_link->disconnectPort();
}

void AxisControllerBase::onLinkLost()
{
    onLinkLostImpl();   // let the subclass cancel anything in flight first

    if (m_controlTimer) m_controlTimer->stop();

    // The position is no longer trustworthy and the origin went with it.
    m_isHomed = false;
}

void AxisControllerBase::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;

    if (!m_controlTimer) return;
    if (active) {
        if (isIdentified()) m_controlTimer->start(20);
    } else {
        m_controlTimer->stop();
    }
}

void AxisControllerBase::applyTuning(const GantryTuning& tuning)
{
    setEncoderCountsPerMm(tuning.countsPerUnit);
    setTravelLimits(tuning.travelLimits);
}

double AxisControllerBase::clampToTravel(double targetUnits)
{
    double clamped = std::clamp(targetUnits, m_travelLimits.minMm, m_travelLimits.maxMm);
    if (clamped != targetUnits) {
        QString msg = QString("Gantry target %1mm clamped to travel limit %2mm")
                          .arg(targetUnits, 0, 'f', 1).arg(clamped, 0, 'f', 1);
        StructuredLogger::instance().log(StructuredLogger::Category::Safety,
            "AxisController", msg);
        emit errorOccurred(msg);
    }
    return clamped;
}
