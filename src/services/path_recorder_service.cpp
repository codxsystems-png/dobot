// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Path Recorder Service
// ═══════════════════════════════════════════════════════════════════════════════

#include "services/path_recorder_service.h"
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace {

double perpendicularDistance(const PathRecorderService::Sample& p,
                              const PathRecorderService::Sample& a,
                              const PathRecorderService::Sample& b)
{
    double dx = b.time - a.time;
    double dy = b.value - a.value;
    double lenSq = dx * dx + dy * dy;

    if (lenSq < 1e-12) {
        // a and b coincide — distance to that single point.
        double ex = p.time - a.time;
        double ey = p.value - a.value;
        return std::sqrt(ex * ex + ey * ey);
    }

    double num = std::abs(dy * p.time - dx * p.value + b.time * a.value - b.value * a.time);
    return num / std::sqrt(lenSq);
}

/// Ramer-Douglas-Peucker: recursively finds the point with the largest
/// perpendicular deviation from the line between the endpoints of
/// [startIdx, endIdx]; keeps it (and recurses on both halves) only if that
/// deviation exceeds tolerance. Interior points that fall within tolerance
/// of the straight line between the two ends they're bracketed by are
/// dropped entirely.
void rdpSimplify(const QList<PathRecorderService::Sample>& pts, int startIdx, int endIdx,
                  double tolerance, QList<int>& keep)
{
    if (endIdx <= startIdx + 1) return;

    double maxDist = 0.0;
    int maxIdx = -1;
    for (int i = startIdx + 1; i < endIdx; ++i) {
        double d = perpendicularDistance(pts[i], pts[startIdx], pts[endIdx]);
        if (d > maxDist) {
            maxDist = d;
            maxIdx = i;
        }
    }

    if (maxIdx >= 0 && maxDist > tolerance) {
        keep.append(maxIdx);
        rdpSimplify(pts, startIdx, maxIdx, tolerance, keep);
        rdpSimplify(pts, maxIdx, endIdx, tolerance, keep);
    }
}

} // namespace

PathRecorderService::PathRecorderService(QObject* parent)
    : QObject(parent)
{
}

void PathRecorderService::startRecording()
{
    m_samples.clear();
    m_clock.restart();
    m_recording = true;
    emit recordingStarted();
}

void PathRecorderService::stopRecording()
{
    if (!m_recording) return;
    m_recording = false;
    emit recordingStopped(m_samples.size());
}

void PathRecorderService::addSample(double value)
{
    if (!m_recording) return;
    m_samples.append({m_clock.elapsed() / 1000.0, value});
}

void PathRecorderService::addSampleAt(double time, double value)
{
    if (!m_recording) return;
    m_samples.append({time, value});
}

void PathRecorderService::clear()
{
    m_samples.clear();
}

QList<GantryKeyframe> PathRecorderService::simplifyToGantryKeyframes(double toleranceMm) const
{
    QList<GantryKeyframe> result;
    if (m_samples.isEmpty()) return result;

    if (m_samples.size() == 1) {
        GantryKeyframe kf;
        kf.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        kf.time = m_samples[0].time;
        kf.positionMm = m_samples[0].value;
        result.append(kf);
        return result;
    }

    QList<int> keep;
    keep.append(0);
    rdpSimplify(m_samples, 0, m_samples.size() - 1, toleranceMm, keep);
    keep.append(m_samples.size() - 1);

    std::sort(keep.begin(), keep.end());
    keep.erase(std::unique(keep.begin(), keep.end()), keep.end());

    for (int idx : keep) {
        GantryKeyframe kf;
        kf.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        kf.time = m_samples[idx].time;
        kf.positionMm = m_samples[idx].value;
        kf.easing = GantryKeyframe::Easing::Linear;
        result.append(kf);
    }

    return result;
}
