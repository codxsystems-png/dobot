// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — FIZ Service
// ═══════════════════════════════════════════════════════════════════════════════

#include "application/fizservice.h"
#include "infrastructure/fiz/nucleusservice.h"
#include <algorithm>
#include <cmath>

FizService::FizService(QObject* parent)
    : QObject(parent)
{
}

void FizService::initialize(NucleusService* nucleus)
{
    m_nucleus = nucleus;
}

// ─── Teaching ───────────────────────────────────────────────────────────────────

void FizService::setFocus(float percent)
{
    if (m_nucleus) m_nucleus->setFocus(percent);
}

void FizService::setIris(float percent)
{
    if (m_nucleus) m_nucleus->setIris(percent);
}

void FizService::setZoom(float percent)
{
    if (m_nucleus) m_nucleus->setZoom(percent);
}

FizState FizService::currentState() const
{
    return m_nucleus ? m_nucleus->currentState() : FizState{};
}

FizState FizService::captureCurrentFiz() const
{
    return currentState();
}

// ─── Keyframe Management ────────────────────────────────────────────────────────

void FizService::addKeyframe(const FizKeyframe& kf)
{
    m_keyframes.append(kf);
    sortKeyframes();
    emit keyframesChanged();
}

void FizService::updateKeyframe(const FizKeyframe& kf)
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

void FizService::removeKeyframe(const QString& id)
{
    m_keyframes.removeIf([&](const FizKeyframe& kf) { return kf.id == id; });
    emit keyframesChanged();
}

void FizService::clearKeyframes()
{
    m_keyframes.clear();
    emit keyframesChanged();
}

void FizService::setKeyframes(const QList<FizKeyframe>& kfs)
{
    m_keyframes = kfs;
    sortKeyframes();
    emit keyframesChanged();
}

void FizService::sortKeyframes()
{
    std::sort(m_keyframes.begin(), m_keyframes.end(),
              [](const FizKeyframe& a, const FizKeyframe& b) {
                  return a.time < b.time;
              });
}

// ─── Interpolation ──────────────────────────────────────────────────────────────

FizState FizService::interpolateAt(double timeSec) const
{
    if (m_keyframes.isEmpty())
        return FizState{0.0f, 0.0f, 0.0f};

    if (m_keyframes.size() == 1)
        return m_keyframes.first().state;

    // Before first keyframe → hold first
    if (timeSec <= m_keyframes.first().time)
        return m_keyframes.first().state;

    // After last keyframe → hold last
    if (timeSec >= m_keyframes.last().time)
        return m_keyframes.last().state;

    // Find surrounding keyframes KF_A and KF_B
    int idxA = 0;
    for (int i = 0; i < m_keyframes.size() - 1; ++i) {
        if (m_keyframes[i + 1].time > timeSec) {
            idxA = i;
            break;
        }
    }

    const FizKeyframe& kfA = m_keyframes[idxA];
    const FizKeyframe& kfB = m_keyframes[idxA + 1];

    double span  = kfB.time - kfA.time;
    double alpha = (span > 0.0) ? (timeSec - kfA.time) / span : 0.0;
    double eased = applyEasing(alpha, kfA.easing);

    FizState result;
    result.focus = lerp(kfA.state.focus, kfB.state.focus, eased);
    result.iris  = lerp(kfA.state.iris,  kfB.state.iris,  eased);
    result.zoom  = lerp(kfA.state.zoom,  kfB.state.zoom,  eased);

    return result;
}

void FizService::sendInterpolatedFrame(double timeSec)
{
    FizState state = interpolateAt(timeSec);
    if (m_nucleus)
        m_nucleus->sendFizFrame(state);
    emit fizStateChanged(state);
}

// ─── Easing Math ────────────────────────────────────────────────────────────────

double FizService::applyEasing(double t, FizKeyframe::Easing e)
{
    t = std::clamp(t, 0.0, 1.0);
    switch (e) {
    case FizKeyframe::Easing::Linear:
        return t;
    case FizKeyframe::Easing::EaseIn:
        return t * t;
    case FizKeyframe::Easing::EaseOut:
        return t * (2.0 - t);
    case FizKeyframe::Easing::EaseInOut:
        return t < 0.5 ? 2.0 * t * t : -1.0 + (4.0 - 2.0 * t) * t;
    default:
        return t;
    }
}

float FizService::lerp(float a, float b, double t)
{
    return a + static_cast<float>(t) * (b - a);
}
