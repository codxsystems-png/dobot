#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — FIZ Service (Phase 7)
// Application-layer: interpolation engine + keyframe management.
// Lives on main thread. Delegates serial I/O to NucleusService.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QList>
#include "core/types.h"

class NucleusService;

class FizService : public QObject
{
    Q_OBJECT
public:
    explicit FizService(QObject* parent = nullptr);

    void initialize(NucleusService* nucleus);

    // ─── Teaching ──────────────────────────────────────────
    void     setFocus(float percent);
    void     setIris(float percent);
    void     setZoom(float percent);
    FizState currentState() const;
    FizState captureCurrentFiz() const;

    // ─── Keyframe management ──────────────────────────────
    void               addKeyframe(const FizKeyframe& kf);
    void               updateKeyframe(const FizKeyframe& kf);
    void               removeKeyframe(const QString& id);
    void               clearKeyframes();
    QList<FizKeyframe> keyframes() const { return m_keyframes; }
    void               setKeyframes(const QList<FizKeyframe>& kfs);

    // ─── Playback (called at 30 Hz by PlaybackService) ────
    FizState interpolateAt(double timeSec) const;
    void     sendInterpolatedFrame(double timeSec);

    // ─── Easing math ──────────────────────────────────────
    static double applyEasing(double t, FizKeyframe::Easing e);
    static float  lerp(float a, float b, double t);

signals:
    void fizStateChanged(const FizState& state);
    void keyframesChanged();

private:
    void sortKeyframes();

    NucleusService*     m_nucleus = nullptr;
    QList<FizKeyframe>  m_keyframes;
};
