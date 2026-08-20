#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Axis Manager
//
// Owns every external axis: the serial links, the controllers on them, and the
// single thread they all live on. Hands out a controller by axis id.
//
// ─── Why one thread for all axes, not one each ───────────────────────────────
// They are 50Hz serial-bound, so a thread each buys nothing, and axes sharing
// a board MUST share a thread anyway — an AxisBoardLink is not safe to drive
// from two threads at once. A thread per axis would make that a latent race
// that only appears when two axes happen to be on one port.
//
// ─── One kind of axis ────────────────────────────────────────────────────────
// Every axis is a step/dir channel into a closed-loop drive. The drive closes
// its own loop internally, so the host runs no PID, holds no gains and has
// nothing to tune — it streams step targets and reads back the count. If a
// servo is fitted later it gets an encoder and the SAME logic, not a second
// control model.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include "core/types.h"

class QThread;
class AxisBoardLink;
class StepperAxisController;

class AxisManager : public QObject
{
    Q_OBJECT
public:
    /// `thread` must outlive the manager and is NOT started here — the caller
    /// owns its lifecycle, because it is usually shared with other services
    /// and started once everything has been moved onto it.
    explicit AxisManager(QThread* axisThread, QObject* parent = nullptr);
    ~AxisManager() override;

    /// Creates the runtime for every configured axis, reusing one board link
    /// per distinct port. Safe to call repeatedly: axes that already exist are
    /// reconfigured rather than rebuilt, so a settings change never drops a
    /// live serial connection.
    ///
    /// Axes beyond kMaxAxes are refused rather than half-created, and a
    /// duplicate board address is refused too — the link routes replies by
    /// index, so a duplicate silently steals another axis's replies.
    void configure(const QList<AxisConfig>& axes);

    StepperAxisController* controller(const QString& axisId) const;

    /// The first axis — "gantry". Everything not yet migrated to per-axis
    /// lookups goes through here.
    StepperAxisController* primary() const;

    /// The board link serving `axisId`, for connect/disconnect at the BOARD
    /// level. Several axes can share one, so connecting is a property of the
    /// board rather than of any single axis.
    AxisBoardLink* linkFor(const QString& axisId) const;

    QStringList axisIds() const { return m_order; }
    bool        hasAxis(const QString& axisId) const { return m_slots.contains(axisId); }

    /// Pushes an axis's persisted configuration to its controller, marshalled
    /// onto the axis thread.
    void applyConfig(const AxisConfig& axis);

signals:
    /// An axis was rejected by configure(). Carries a reason fit to show the
    /// operator: silently dropping an axis would leave them with a configured
    /// axis that never moves and no indication why.
    void axisRejected(const QString& axisId, const QString& reason);

private:
    struct Slot {
        AxisConfig             config;
        AxisBoardLink*         link       = nullptr;
        StepperAxisController* controller = nullptr;
    };

    void           buildSlot(const AxisConfig& axis);
    AxisBoardLink* linkForPort(const QString& portName);

    QThread*                       m_thread = nullptr;
    QHash<QString, Slot>           m_slots;
    QStringList                    m_order;   // configuration order; first is primary
    QHash<QString, AxisBoardLink*> m_linksByPort;
};
