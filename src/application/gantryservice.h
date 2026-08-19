#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Gantry Service
// Application-layer: interpolation engine + keyframe management for the linear gantry.
// Lives on main thread. Delegates serial I/O to the axis controller.
//
// It holds AxisControllerBase, not a concrete drive kind: interpolation and
// streaming need position, connected/homed state and tick() — all of which
// every axis kind has. That is what lets a stepper run the timeline without
// the playback path knowing steppers exist.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QList>
#include "core/types.h"

class AxisControllerBase;

class GantryService : public QObject
{
    Q_OBJECT
public:
    explicit GantryService(QObject* parent = nullptr);

    void initialize(AxisControllerBase* axisController);

    // ─── Teaching ──────────────────────────────────────────
    double currentPositionMm() const;

    // ─── Keyframe management ──────────────────────────────
    void                 addKeyframe(const GantryKeyframe& kf);
    void                 updateKeyframe(const GantryKeyframe& kf);
    void                 removeKeyframe(const QString& id);
    void                 clearKeyframes();
    QList<GantryKeyframe> keyframes() const { return m_keyframes; }
    void                 setKeyframes(const QList<GantryKeyframe>& kfs);

    // ─── Playback (called at 30 Hz by PlaybackService) ────
    double interpolateAt(double timeSec) const;
    void   sendInterpolatedFrame(double timeSec);
    
    // Heartbeat when playback tick is not running
    void   sendHeartbeat();

    // ─── Easing math ──────────────────────────────────────
    static double applyEasing(double t, GantryKeyframe::Easing e);
    static double lerp(double a, double b, double t);

signals:
    void keyframesChanged();

private:
    void sortKeyframes();

    AxisControllerBase*     m_controller = nullptr;
    QList<GantryKeyframe>   m_keyframes;
};
