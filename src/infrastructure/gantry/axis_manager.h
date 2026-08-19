#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Axis Manager
//
// Owns every external axis: the serial links, the controllers on them, and the
// single thread they all live on. Hands out a controller by axis id and keeps
// exactly one active per axis slot.
//
// ─── Why one thread for all axes, not one each ───────────────────────────────
// They are 50Hz serial-bound, so a thread each buys nothing, and axes sharing
// a board MUST share a thread anyway — an AxisBoardLink is not safe to drive
// from two threads at once. A thread per axis would make that a latent race
// that only appears when two axes happen to be on one port.
//
// ─── Why both drive kinds are built per axis ─────────────────────────────────
// A DC servo and a stepper are different classes, and which one an axis needs
// is a project setting the operator can change at any time. Building only the
// selected kind means tearing down and re-establishing the serial connection
// on every change; building both and putting one to sleep costs an idle
// QObject and nothing else. The sleeping one issues no polls (see
// AxisControllerBase::setActive) but still receives faults for its own axis.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QHash>
#include <QList>
#include <QString>
#include "core/types.h"

class QThread;
class AxisBoardLink;
class AxisControllerBase;
class GantryAxisController;
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
    /// re-selected rather than rebuilt, so a settings change never drops a
    /// live serial connection.
    void configure(const QList<AxisConfig>& axes);

    /// The controller currently driving `axisId`, or nullptr if unknown.
    /// Which concrete kind this is follows the axis's configured driveKind.
    AxisControllerBase* controller(const QString& axisId) const;

    /// The first axis — "gantry". Everything not yet migrated to per-axis
    /// lookups goes through here, which is what lets the rest of the codebase
    /// move over one call site at a time instead of in one sweep.
    AxisControllerBase* primary() const;

    /// The concrete controllers for an axis, REGARDLESS of which is active.
    ///
    /// Needed because some consumers are drive-kind specific rather than
    /// "whatever is driving": the PID tuning dialog only ever means the DC
    /// controller, and it must stay reachable while a stepper is selected —
    /// casting the active controller instead yields nullptr and whatever
    /// used it silently stops working.
    GantryAxisController*  dcController(const QString& axisId) const;
    StepperAxisController* stepperController(const QString& axisId) const;

    /// The board link serving `axisId`, for connect/disconnect at the BOARD
    /// level. Several axes can share one, so connecting is a property of the
    /// board rather than of any single axis.
    AxisBoardLink* linkFor(const QString& axisId) const;

    QStringList axisIds() const { return m_order; }
    bool        hasAxis(const QString& axisId) const { return m_slots.contains(axisId); }

    /// Pushes an axis's persisted configuration to whichever controller is
    /// driving it, marshalled onto the axis thread.
    void applyConfig(const AxisConfig& axis);

signals:
    /// A different concrete controller now drives this axis, because its
    /// drive kind changed. Consumers holding a pointer must re-fetch it —
    /// notably the playback adapter, which otherwise keeps streaming to the
    /// axis that is no longer being driven.
    void activeControllerChanged(const QString& axisId, AxisControllerBase* controller);

private:
    struct Slot {
        AxisConfig             config;
        AxisBoardLink*         link    = nullptr;
        GantryAxisController*  dc      = nullptr;
        StepperAxisController* stepper = nullptr;
        AxisControllerBase*    active  = nullptr;
    };

    void   buildSlot(const AxisConfig& axis);
    void   selectForDriveKind(Slot& slot);
    AxisBoardLink* linkForPort(const QString& portName);

    QThread*                m_thread = nullptr;
    QHash<QString, Slot>    m_slots;
    QStringList             m_order;      // configuration order; first is primary
    QHash<QString, AxisBoardLink*> m_linksByPort;
};
