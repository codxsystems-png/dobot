// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Gantry Service
// ═══════════════════════════════════════════════════════════════════════════════

#include "application/gantryservice.h"
#include "infrastructure/gantry/axis_controller_base.h"
#include <algorithm>
#include <cmath>

GantryService::GantryService(QObject* parent)
    : QObject(parent)
{
}

void GantryService::initialize(AxisControllerBase* axisController)
{
    m_controller = axisController;
}

// ─── Teaching ───────────────────────────────────────────────────────────────────

double GantryService::currentPositionMm() const
{
    return m_controller ? m_controller->currentPositionMm() : 0.0;
}

// ─── Keyframe Management ────────────────────────────────────────────────────────

void GantryService::addKeyframe(const GantryKeyframe& kf)
{
    m_keyframes.append(kf);
    sortKeyframes();
    emit keyframesChanged();
}

void GantryService::updateKeyframe(const GantryKeyframe& kf)
{
    for (int i = 0; i < m_keyframes.size(); ++i) {
        if (m_keyframes[i].id == kf.id) {
            m_keyframes[i] = kf;
            sortKeyframes();
            emit keyframesChanged();
            return;
        }
    }
}

void GantryService::removeKeyframe(const QString& id)
{
    m_keyframes.removeIf([&](const GantryKeyframe& kf) { return kf.id == id; });
    emit keyframesChanged();
}

void GantryService::clearKeyframes()
{
    m_keyframes.clear();
    emit keyframesChanged();
}

void GantryService::setKeyframes(const QList<GantryKeyframe>& kfs)
{
    m_keyframes = kfs;
    sortKeyframes();
    emit keyframesChanged();
}

void GantryService::sortKeyframes()
{
    std::sort(m_keyframes.begin(), m_keyframes.end(),
              [](const GantryKeyframe& a, const GantryKeyframe& b) {
                  return a.time < b.time;
              });
}

// ─── Interpolation ──────────────────────────────────────────────────────────────

double GantryService::interpolateAt(double timeSec) const
{
    if (m_keyframes.isEmpty())
        return m_controller ? m_controller->currentPositionMm() : 0.0;

    // STEP MODE: Find the latest keyframe before or exactly at timeSec
    for (int i = m_keyframes.size() - 1; i >= 0; --i) {
        if (timeSec >= m_keyframes[i].time) {
            return m_keyframes[i].positionMm;
        }
    }

    // Before first keyframe: hold current physical position
    return m_controller ? m_controller->currentPositionMm() : 0.0;
}

void GantryService::sendInterpolatedFrame(double timeSec)
{
    if (!m_controller || !m_controller->isConnected()) return;
    double targetMm = interpolateAt(timeSec);
    m_controller->tick(targetMm);
}

void GantryService::sendHeartbeat()
{
    if (m_controller) {
        m_controller->heartbeat();
    }
}

// ─── Easing Math ────────────────────────────────────────────────────────────────

double GantryService::applyEasing(double t, GantryKeyframe::Easing e)
{
    t = std::clamp(t, 0.0, 1.0);
    switch (e) {
    case GantryKeyframe::Easing::Linear:
        return t;
    case GantryKeyframe::Easing::EaseIn:
        return t * t;
    case GantryKeyframe::Easing::EaseOut:
        return t * (2.0 - t);
    case GantryKeyframe::Easing::EaseInOut:
        return t < 0.5 ? 2.0 * t * t : -1.0 + (4.0 - 2.0 * t) * t;
    default:
        return t;
    }
}

double GantryService::lerp(double a, double b, double t)
{
    return a + t * (b - a);
}
