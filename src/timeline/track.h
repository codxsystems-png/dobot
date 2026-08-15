#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Timeline Tracks (Unified Model)
// ═══════════════════════════════════════════════════════════════════════════════

#include <QVariant>
#include <QString>
#include <QList>

namespace timeline {

struct TrackKeyframe {
    QString id;
    double time; // seconds
    QVariant value;
    
    // Limits / settings for the segment leading out of this keyframe
    double maxVelocity = 0.0;
    double maxAcceleration = 0.0;
};

class ITimelineTrack {
public:
    virtual ~ITimelineTrack() = default;

    virtual QString trackId() const = 0;
    
    // Core stateless evaluation: Get the value at time t
    virtual QVariant sampleAt(double t) const = 0;
    
    // Pre-flight validation
    virtual bool validateLimits(QString& outError) const = 0;

    // Keyframe management
    virtual void addKeyframe(const TrackKeyframe& kf) = 0;
    virtual void clearKeyframes() = 0;
    virtual QList<TrackKeyframe> keyframes() const = 0;
};

} // namespace timeline
