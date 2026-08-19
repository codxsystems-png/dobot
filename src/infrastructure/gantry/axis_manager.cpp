// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Axis Manager
// ═══════════════════════════════════════════════════════════════════════════════

#include "infrastructure/gantry/axis_manager.h"
#include "infrastructure/gantry/axis_board_link.h"
#include "infrastructure/gantry/gantryaxiscontroller.h"
#include "infrastructure/gantry/stepper_axis_controller.h"
#include "core/structured_logger.h"
#include <QThread>
#include <QMetaObject>

AxisManager::AxisManager(QThread* axisThread, QObject* parent)
    : QObject(parent)
    , m_thread(axisThread)
{
}

AxisManager::~AxisManager() = default;

AxisBoardLink* AxisManager::linkForPort(const QString& portName)
{
    // Axes on the same board share one link, keyed by port. An empty port is
    // still a valid key: several axes on an as-yet-unchosen port must end up
    // on the SAME link, or picking the port later would leave them talking
    // over separate transports to one physical board.
    auto it = m_linksByPort.constFind(portName);
    if (it != m_linksByPort.constEnd()) return *it;

    auto* link = new AxisBoardLink();
    if (m_thread) link->moveToThread(m_thread);
    m_linksByPort.insert(portName, link);
    return link;
}

void AxisManager::buildSlot(const AxisConfig& axis)
{
    Slot slot;
    slot.config = axis;
    slot.link   = linkForPort(axis.portName);

    // Both kinds, always. See the header for why building only the selected
    // one would cost a serial reconnect on every settings change.
    slot.dc      = new GantryAxisController(slot.link, axis.firmwareAxisIndex);
    slot.stepper = new StepperAxisController(slot.link, axis.firmwareAxisIndex);
    if (m_thread) {
        slot.dc->moveToThread(m_thread);
        slot.stepper->moveToThread(m_thread);
    }

    // Both start silenced, then selectForDriveKind wakes exactly one. Doing
    // it in that order means there is never a window with two controllers
    // polling the same axis address.
    slot.dc->setActive(false);
    slot.stepper->setActive(false);

    m_slots.insert(axis.id, slot);
    m_order.append(axis.id);

    selectForDriveKind(m_slots[axis.id]);
}

void AxisManager::selectForDriveKind(Slot& slot)
{
    const bool stepper = slot.config.motorSpec.driveKind == AxisDriveKind::StepDirClosedLoop;
    AxisControllerBase* wanted = stepper
        ? static_cast<AxisControllerBase*>(slot.stepper)
        : static_cast<AxisControllerBase*>(slot.dc);
    AxisControllerBase* other  = stepper
        ? static_cast<AxisControllerBase*>(slot.dc)
        : static_cast<AxisControllerBase*>(slot.stepper);

    if (slot.active == wanted) return;

    AxisControllerBase* previous = slot.active;
    slot.active = wanted;

    // Queued: both controllers live on the axis thread. Stop the outgoing one
    // before waking the incoming one, so a jog in progress cannot outlive the
    // switch and keep driving an axis nobody is watching.
    if (previous) {
        QMetaObject::invokeMethod(previous, [previous]() {
            previous->stopJog();
            previous->setActive(false);
        }, Qt::QueuedConnection);
    } else {
        QMetaObject::invokeMethod(other, [other]() { other->setActive(false); },
                                  Qt::QueuedConnection);
    }
    QMetaObject::invokeMethod(wanted, [wanted]() { wanted->setActive(true); },
                              Qt::QueuedConnection);

    StructuredLogger::instance().log(StructuredLogger::Category::Motion, "AxisManager",
        QString("Axis '%1' is now driven as %2.")
            .arg(slot.config.id)
            .arg(stepper ? "a step/dir stepper" : "a DC servo"));

    emit activeControllerChanged(slot.config.id, wanted);
}

void AxisManager::configure(const QList<AxisConfig>& axes)
{
    for (const AxisConfig& axis : axes) {
        auto it = m_slots.find(axis.id);
        if (it == m_slots.end()) {
            buildSlot(axis);
        } else {
            // Existing axis: update its configuration and re-select. Rebuilding
            // would drop a live serial connection every time the operator
            // touched an unrelated setting.
            it->config = axis;
            selectForDriveKind(*it);
        }
        applyConfig(axis);
    }
}

void AxisManager::applyConfig(const AxisConfig& axis)
{
    auto it = m_slots.constFind(axis.id);
    if (it == m_slots.constEnd() || !it->active) return;

    AxisControllerBase* active = it->active;
    auto* stepper = (active == it->stepper) ? it->stepper : nullptr;
    const GantryTuning    tuning = axis.tuning;
    const GantryMotorSpec spec   = axis.motorSpec;

    QMetaObject::invokeMethod(active, [active, stepper, tuning, spec]() {
        if (stepper) {
            stepper->setStepRateLimits(static_cast<long>(spec.stepRateCeilingHz),
                                       static_cast<long>(tuning.stepAccelStepsPerSec2));
            stepper->setIdleDisable(tuning.idleDisable);
        }
        active->applyTuning(tuning);
    }, Qt::QueuedConnection);
}

AxisControllerBase* AxisManager::controller(const QString& axisId) const
{
    auto it = m_slots.constFind(axisId);
    return (it == m_slots.constEnd()) ? nullptr : it->active;
}

AxisControllerBase* AxisManager::primary() const
{
    if (m_order.isEmpty()) return nullptr;
    return controller(m_order.first());
}

AxisBoardLink* AxisManager::linkFor(const QString& axisId) const
{
    auto it = m_slots.constFind(axisId);
    return (it == m_slots.constEnd()) ? nullptr : it->link;
}
