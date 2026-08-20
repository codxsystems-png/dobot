// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Axis Manager
// ═══════════════════════════════════════════════════════════════════════════════

#include "infrastructure/gantry/axis_manager.h"
#include "infrastructure/gantry/axis_board_link.h"
#include "infrastructure/gantry/stepper_axis_controller.h"
#include "core/structured_logger.h"
#include <QThread>
#include <QMetaObject>
#include <QSet>

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
    slot.config     = axis;
    slot.link       = linkForPort(axis.portName);
    slot.controller = new StepperAxisController(slot.link, axis.firmwareAxisIndex);
    if (m_thread) slot.controller->moveToThread(m_thread);

    m_slots.insert(axis.id, slot);
    m_order.append(axis.id);
}

void AxisManager::configure(const QList<AxisConfig>& axes)
{
    // Board addresses must be unique per port. The link routes replies by
    // index, so two axes sharing one would leave the second silently taking
    // over the first's position, faults and limit switch — with no error
    // anywhere. Checked here rather than trusted, because it has happened.
    QHash<QString, QSet<int>> usedIndices;

    for (const AxisConfig& axis : axes) {
        if (axis.id.isEmpty()) continue;

        const bool isNew = !m_slots.contains(axis.id);

        if (isNew && m_order.size() >= kMaxAxes) {
            const QString why = QString("This board drives at most %1 axes.").arg(kMaxAxes);
            StructuredLogger::instance().log(StructuredLogger::Category::Safety,
                "AxisManager", QString("Axis '%1' refused: %2").arg(axis.id, why));
            emit axisRejected(axis.id, why);
            continue;
        }

        QSet<int>& taken = usedIndices[axis.portName];
        if (taken.contains(axis.firmwareAxisIndex)) {
            const QString why =
                QString("Board address %1 is already used by another axis on %2.")
                    .arg(axis.firmwareAxisIndex)
                    .arg(axis.portName.isEmpty() ? QStringLiteral("this board") : axis.portName);
            StructuredLogger::instance().log(StructuredLogger::Category::Safety,
                "AxisManager", QString("Axis '%1' refused: %2").arg(axis.id, why));
            emit axisRejected(axis.id, why);
            continue;
        }
        taken.insert(axis.firmwareAxisIndex);

        if (isNew) {
            buildSlot(axis);
            StructuredLogger::instance().log(StructuredLogger::Category::Motion,
                "AxisManager",
                QString("Axis '%1' created at board address %2.")
                    .arg(axis.id).arg(axis.firmwareAxisIndex));
        } else {
            // Reconfigure in place. Rebuilding would drop a live serial
            // connection every time the operator touched an unrelated field.
            m_slots[axis.id].config = axis;
        }
        applyConfig(axis);
    }
}

void AxisManager::applyConfig(const AxisConfig& axis)
{
    auto it = m_slots.constFind(axis.id);
    if (it == m_slots.constEnd() || !it->controller) return;

    StepperAxisController* c = it->controller;
    const GantryTuning    tuning = axis.tuning;
    const GantryMotorSpec spec   = axis.motorSpec;

    QMetaObject::invokeMethod(c, [c, tuning, spec]() {
        c->setStepRateLimits(static_cast<long>(spec.stepRateCeilingHz),
                             static_cast<long>(tuning.stepAccelStepsPerSec2));
        c->setIdleDisable(tuning.idleDisable);
        c->applyTuning(tuning);
    }, Qt::QueuedConnection);
}

StepperAxisController* AxisManager::controller(const QString& axisId) const
{
    auto it = m_slots.constFind(axisId);
    return (it == m_slots.constEnd()) ? nullptr : it->controller;
}

StepperAxisController* AxisManager::primary() const
{
    if (m_order.isEmpty()) return nullptr;
    return controller(m_order.first());
}

AxisBoardLink* AxisManager::linkFor(const QString& axisId) const
{
    auto it = m_slots.constFind(axisId);
    return (it == m_slots.constEnd()) ? nullptr : it->link;
}
