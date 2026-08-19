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

/// How a track's values reach its device.
///
/// This replaces the engine comparing track ids against the string literals
/// "robot", "gantry" and "fiz". Those comparisons meant a new axis could not
/// be added without editing the playback engine, and — worse — an axis whose
/// id was merely misspelled would be silently skipped rather than failing
/// loudly. The behaviour belongs to the track, so the track states it.
enum class DeliveryMode {
    /// Continuous setpoints, every tick. sampleAt() is meaningful at any t.
    Streamed,
    /// Discrete move commands fired at keyframe times; the device runs its
    /// own profile and takes real time to get there.
    Waypoint,
    /// Either, decided by the playback mode. The robot can be streamed with
    /// ServoJ or fired waypoint-to-waypoint, and only the engine knows which
    /// is in force.
    StreamedOrWaypoint
};

class ITimelineTrack {
public:
    virtual ~ITimelineTrack() = default;

    virtual QString trackId() const = 0;

    /// Defaults to Streamed: an external axis is the common case, and a new
    /// axis that forgets to say gets the behaviour that merely looks wrong
    /// rather than the one that fires uncommanded moves.
    virtual DeliveryMode deliveryMode() const { return DeliveryMode::Streamed; }
    
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
