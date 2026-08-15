#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Gantry Service
// Application-layer: interpolation engine + keyframe management for the linear gantry.
// Lives on main thread. Delegates serial I/O to GantryAxisController.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QList>
#include "core/types.h"

class GantryAxisController;

class GantryService : public QObject
{
    Q_OBJECT
public:
    explicit GantryService(QObject* parent = nullptr);

    void initialize(GantryAxisController* gantryController);

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

    GantryAxisController*   m_controller = nullptr;
    QList<GantryKeyframe>   m_keyframes;
};
