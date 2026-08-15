#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Track Implementations
// ═══════════════════════════════════════════════════════════════════════════════

#include "timeline/track.h"
#include "math/motion_profile.h"
#include "core/types.h"
#include "hardware/dobot_adapter.h"

namespace timeline {

// ─── Base Track ────────────────────────────────────────────────────────────────
class BaseTrack : public ITimelineTrack {
public:
    explicit BaseTrack(const QString& id) : m_id(id) {}
    
    QString trackId() const override { return m_id; }
    
    void addKeyframe(const TrackKeyframe& kf) override {
        m_keyframes.append(kf);
        // Ensure sorted by time
        std::sort(m_keyframes.begin(), m_keyframes.end(), [](const TrackKeyframe& a, const TrackKeyframe& b) {
            return a.time < b.time;
        });
    }
    
    void clearKeyframes() override {
        m_keyframes.clear();
    }
    
    QList<TrackKeyframe> keyframes() const override {
        return m_keyframes;
    }

protected:
    QString m_id;
    QList<TrackKeyframe> m_keyframes;
};


// ─── Robot Track ───────────────────────────────────────────────────────────────
class RobotTrack : public BaseTrack {
public:
    explicit RobotTrack(const QString& id) : BaseTrack(id) {}

    void setWorkspaceLimits(const WorkspaceLimits& limits) { m_workspace = limits; }

    // Cartesian velocity/accel used for Mode 2 (Streamed) interpolation only
    // — Fire-Together (Mode 1) ignores this and lets the Dobot firmware's own
    // MovJ/MovL/Arc planner handle the move. ASSUMPTION: rough Nova 5
    // Cartesian-speed placeholder, needs tuning against real hardware.
    void setLimits(double maxVelocityMmPerSec, double maxAccelMmPerSec2) {
        m_maxVelocity = maxVelocityMmPerSec;
        m_maxAcceleration = maxAccelMmPerSec2;
    }

    QVariant sampleAt(double t) const override {
        // Only meaningful for Mode 2 (Streamed) playback — PlaybackEngine
        // only calls sendStreamedSetpoint() with this for the robot track
        // when explicitly running in Mode::Streamed; Fire-Together ignores
        // continuous sampling entirely and fires discrete keyframes instead.
        if (m_keyframes.isEmpty()) return QVariant();
        if (t <= m_keyframes.first().time) return m_keyframes.first().value;
        if (t >= m_keyframes.last().time) return m_keyframes.last().value;

        for (int i = 0; i < m_keyframes.size() - 1; ++i) {
            const TrackKeyframe& kf0 = m_keyframes[i];
            const TrackKeyframe& kf1 = m_keyframes[i + 1];
            if (t < kf0.time || t > kf1.time) continue;

            double duration = kf1.time - kf0.time;
            if (!kf0.value.canConvert<hardware::DobotMoveTarget>() ||
                !kf1.value.canConvert<hardware::DobotMoveTarget>()) {
                return kf1.value;
            }

            hardware::DobotMoveTarget t0 = kf0.value.value<hardware::DobotMoveTarget>();
            hardware::DobotMoveTarget t1 = kf1.value.value<hardware::DobotMoveTarget>();
            if (duration < 1e-6) return QVariant::fromValue(t1);

            double vMax = kf1.maxVelocity > 0.0 ? kf1.maxVelocity : m_maxVelocity;
            double aMax = kf1.maxAcceleration > 0.0 ? kf1.maxAcceleration : m_maxAcceleration;
            double localT = t - kf0.time;

            // Independent trapezoidal profile per Cartesian axis, all synced
            // to [kf0.time, kf1.time] so every axis (and the gantry/FIZ
            // tracks alongside it) lands together at kf1.time.
            auto xP  = math::TrapezoidalProfile::fitToDuration(t0.targetPose.x,  t1.targetPose.x,  vMax, aMax, duration);
            auto yP  = math::TrapezoidalProfile::fitToDuration(t0.targetPose.y,  t1.targetPose.y,  vMax, aMax, duration);
            auto zP  = math::TrapezoidalProfile::fitToDuration(t0.targetPose.z,  t1.targetPose.z,  vMax, aMax, duration);
            auto rxP = math::TrapezoidalProfile::fitToDuration(t0.targetPose.rx, t1.targetPose.rx, vMax, aMax, duration);
            auto ryP = math::TrapezoidalProfile::fitToDuration(t0.targetPose.ry, t1.targetPose.ry, vMax, aMax, duration);
            auto rzP = math::TrapezoidalProfile::fitToDuration(t0.targetPose.rz, t1.targetPose.rz, vMax, aMax, duration);

            hardware::DobotMoveTarget interp = t1; // carry forward speed/acc/moveType metadata
            interp.targetPose.x  = xP.sampleAt(localT).position;
            interp.targetPose.y  = yP.sampleAt(localT).position;
            interp.targetPose.z  = zP.sampleAt(localT).position;
            interp.targetPose.rx = rxP.sampleAt(localT).position;
            interp.targetPose.ry = ryP.sampleAt(localT).position;
            interp.targetPose.rz = rzP.sampleAt(localT).position;

            return QVariant::fromValue(interp);
        }
        return m_keyframes.last().value;
    }

    bool validateLimits(QString& outError) const override {
        for (const auto& kf : m_keyframes) {
            if (!kf.value.canConvert<hardware::DobotMoveTarget>()) {
                outError = "Invalid keyframe payload for RobotTrack";
                return false;
            }

            hardware::DobotMoveTarget tgt = kf.value.value<hardware::DobotMoveTarget>();
            if (!poseWithinWorkspace(tgt.targetPose, kf.id, outError)) {
                return false;
            }
            if (tgt.moveType == TimelineSegment::Arc &&
                !poseWithinWorkspace(tgt.viaPose, kf.id + " (arc via-point)", outError)) {
                return false;
            }
        }
        return true;
    }

private:
    bool poseWithinWorkspace(const CartesianPose& pose, const QString& label, QString& outError) const {
        if (pose.x < m_workspace.minX || pose.x > m_workspace.maxX ||
            pose.y < m_workspace.minY || pose.y > m_workspace.maxY ||
            pose.z < m_workspace.minZ || pose.z > m_workspace.maxZ) {
            outError = QString("Robot keyframe '%1' pose (%2, %3, %4) is outside the workspace envelope "
                                "[%5,%6]x[%7,%8]x[%9,%10]")
                .arg(label)
                .arg(pose.x, 0, 'f', 1).arg(pose.y, 0, 'f', 1).arg(pose.z, 0, 'f', 1)
                .arg(m_workspace.minX, 0, 'f', 1).arg(m_workspace.maxX, 0, 'f', 1)
                .arg(m_workspace.minY, 0, 'f', 1).arg(m_workspace.maxY, 0, 'f', 1)
                .arg(m_workspace.minZ, 0, 'f', 1).arg(m_workspace.maxZ, 0, 'f', 1);
            return false;
        }
        return true;
    }

    WorkspaceLimits m_workspace;
    double m_maxVelocity = 200.0;     // mm/s (approximate — rotation axes share this too)
    double m_maxAcceleration = 400.0; // mm/s^2
};

// ─── FIZ Track ─────────────────────────────────────────────────────────────────
class FizTrack : public BaseTrack {
public:
    explicit FizTrack(const QString& id) : BaseTrack(id) {}

    // ASSUMPTION: verify against Tilta Nucleus-M motor speed/accel specs.
    // Per-keyframe TrackKeyframe::maxVelocity/maxAcceleration (set > 0)
    // override these track-wide defaults for that segment.
    void setLimits(double maxVelocityPctPerSec, double maxAccelPctPerSec2) {
        m_maxVelocity = maxVelocityPctPerSec;
        m_maxAcceleration = maxAccelPctPerSec2;
    }

    void setRangeLimits(const FizLimits& limits) { m_range = limits; }

    QVariant sampleAt(double t) const override {
        if (m_keyframes.isEmpty()) return QVariant();
        if (t <= m_keyframes.first().time) return m_keyframes.first().value;
        if (t >= m_keyframes.last().time) return m_keyframes.last().value;

        for (int i = 0; i < m_keyframes.size() - 1; ++i) {
            const TrackKeyframe& kf0 = m_keyframes[i];
            const TrackKeyframe& kf1 = m_keyframes[i + 1];
            if (t >= kf0.time && t <= kf1.time) {
                double duration = kf1.time - kf0.time;

                FizState s0 = kf0.value.value<FizState>();
                FizState s1 = kf1.value.value<FizState>();

                if (duration < 1e-6) return QVariant::fromValue(s1);

                double vMax = kf1.maxVelocity > 0.0 ? kf1.maxVelocity : m_maxVelocity;
                double aMax = kf1.maxAcceleration > 0.0 ? kf1.maxAcceleration : m_maxAcceleration;
                double localT = t - kf0.time;

                // Independent trapezoidal profile per channel, all synced to
                // the same [kf0.time, kf1.time] window so focus/iris/zoom
                // and every other track land together at kf1.time.
                auto focusProfile = math::TrapezoidalProfile::fitToDuration(s0.focus, s1.focus, vMax, aMax, duration);
                auto irisProfile  = math::TrapezoidalProfile::fitToDuration(s0.iris,  s1.iris,  vMax, aMax, duration);
                auto zoomProfile  = math::TrapezoidalProfile::fitToDuration(s0.zoom,  s1.zoom,  vMax, aMax, duration);

                FizState interp;
                interp.focus = static_cast<float>(focusProfile.sampleAt(localT).position);
                interp.iris  = static_cast<float>(irisProfile.sampleAt(localT).position);
                interp.zoom  = static_cast<float>(zoomProfile.sampleAt(localT).position);

                return QVariant::fromValue(interp);
            }
        }
        return m_keyframes.last().value;
    }

    bool validateLimits(QString& outError) const override {
        for (const auto& kf : m_keyframes) {
            FizState s = kf.value.value<FizState>();
            if (!channelInRange(s.focus, "focus", kf.id, outError)) return false;
            if (!channelInRange(s.iris,  "iris",  kf.id, outError)) return false;
            if (!channelInRange(s.zoom,  "zoom",  kf.id, outError)) return false;
        }

        for (int i = 0; i < m_keyframes.size() - 1; ++i) {
            const TrackKeyframe& kf0 = m_keyframes[i];
            const TrackKeyframe& kf1 = m_keyframes[i + 1];
            double duration = kf1.time - kf0.time;
            if (duration <= 0.0) continue;

            double vMax = kf1.maxVelocity > 0.0 ? kf1.maxVelocity : m_maxVelocity;
            double aMax = kf1.maxAcceleration > 0.0 ? kf1.maxAcceleration : m_maxAcceleration;

            FizState s0 = kf0.value.value<FizState>();
            FizState s1 = kf1.value.value<FizState>();
            if (!channelFeasible(s0.focus, s1.focus, vMax, aMax, duration, "focus", kf0.time, kf1.time, outError)) return false;
            if (!channelFeasible(s0.iris,  s1.iris,  vMax, aMax, duration, "iris",  kf0.time, kf1.time, outError)) return false;
            if (!channelFeasible(s0.zoom,  s1.zoom,  vMax, aMax, duration, "zoom",  kf0.time, kf1.time, outError)) return false;
        }
        return true;
    }

private:
    bool channelInRange(float value, const char* channel, const QString& kfId, QString& outError) const {
        if (value < m_range.minPercent || value > m_range.maxPercent) {
            outError = QString("FIZ keyframe '%1' %2=%3%% is outside bounds [%4,%5]%%")
                .arg(kfId, channel)
                .arg(value, 0, 'f', 1)
                .arg(m_range.minPercent, 0, 'f', 1).arg(m_range.maxPercent, 0, 'f', 1);
            return false;
        }
        return true;
    }

    bool channelFeasible(double v0, double v1, double vMax, double aMax, double duration,
                          const char* channel, double kf0Time, double kf1Time,
                          QString& outError) const {
        math::TrapezoidalProfile natural(v0, v1, vMax, aMax);
        if (natural.duration() > duration + 1e-6) {
            outError = QString("FIZ %1 move at %2s -> %3s needs %4s at the configured speed/accel "
                                "limits but only %5s is available. Drag the %1 track keyframes at "
                                "those times further apart.")
                .arg(channel)
                .arg(kf0Time, 0, 'f', 2).arg(kf1Time, 0, 'f', 2)
                .arg(natural.duration(), 0, 'f', 2).arg(duration, 0, 'f', 2);
            return false;
        }
        return true;
    }

    double m_maxVelocity = 40.0;      // %/s
    double m_maxAcceleration = 150.0; // %/s^2
    FizLimits m_range;
};

// ─── Gantry Track ──────────────────────────────────────────────────────────────
class GantryTrack : public BaseTrack {
public:
    explicit GantryTrack(const QString& id) : BaseTrack(id) {}

    // ASSUMPTION: verify against real gantry hardware (rail length, motor,
    // gearing). Per-keyframe TrackKeyframe::maxVelocity/maxAcceleration
    // (set > 0) override these track-wide defaults for that segment.
    void setLimits(double maxVelocityMmPerSec, double maxAccelMmPerSec2) {
        m_maxVelocity = maxVelocityMmPerSec;
        m_maxAcceleration = maxAccelMmPerSec2;
    }

    void setRangeLimits(const GantryLimits& limits) { m_range = limits; }

    QVariant sampleAt(double t) const override {
        if (m_keyframes.isEmpty()) return QVariant();
        if (t <= m_keyframes.first().time) return m_keyframes.first().value;
        if (t >= m_keyframes.last().time) return m_keyframes.last().value;

        for (int i = 0; i < m_keyframes.size() - 1; ++i) {
            const TrackKeyframe& kf0 = m_keyframes[i];
            const TrackKeyframe& kf1 = m_keyframes[i + 1];
            if (t >= kf0.time && t <= kf1.time) {
                double duration = kf1.time - kf0.time;
                double val0 = kf0.value.toDouble();
                double val1 = kf1.value.toDouble();

                if (duration < 1e-6) return QVariant::fromValue(val1);

                double vMax = kf1.maxVelocity > 0.0 ? kf1.maxVelocity : m_maxVelocity;
                double aMax = kf1.maxAcceleration > 0.0 ? kf1.maxAcceleration : m_maxAcceleration;

                auto profile = math::TrapezoidalProfile::fitToDuration(val0, val1, vMax, aMax, duration);
                return QVariant::fromValue(profile.sampleAt(t - kf0.time).position);
            }
        }
        return m_keyframes.last().value;
    }

    bool validateLimits(QString& outError) const override {
        for (const auto& kf : m_keyframes) {
            double posMm = kf.value.toDouble();
            if (posMm < m_range.minMm || posMm > m_range.maxMm) {
                outError = QString("Gantry keyframe '%1' at %2mm is outside travel range [%3,%4]mm")
                    .arg(kf.id)
                    .arg(posMm, 0, 'f', 1)
                    .arg(m_range.minMm, 0, 'f', 1).arg(m_range.maxMm, 0, 'f', 1);
                return false;
            }
        }

        for (int i = 0; i < m_keyframes.size() - 1; ++i) {
            const TrackKeyframe& kf0 = m_keyframes[i];
            const TrackKeyframe& kf1 = m_keyframes[i + 1];
            double duration = kf1.time - kf0.time;
            if (duration <= 0.0) continue;

            double vMax = kf1.maxVelocity > 0.0 ? kf1.maxVelocity : m_maxVelocity;
            double aMax = kf1.maxAcceleration > 0.0 ? kf1.maxAcceleration : m_maxAcceleration;
            double val0 = kf0.value.toDouble();
            double val1 = kf1.value.toDouble();

            math::TrapezoidalProfile natural(val0, val1, vMax, aMax);
            if (natural.duration() > duration + 1e-6) {
                // Times, not raw ids — these are the diamond markers on the
                // GANTRY curve below the timeline (independent of the segment
                // blocks above), so "id" alone gave no way to find them.
                outError = QString("Gantry move at %1s -> %2s needs %3s at the configured speed/accel "
                                    "limits but only %4s is available. Drag the GANTRY track keyframes "
                                    "at those times further apart, or move them closer together in "
                                    "position.")
                    .arg(kf0.time, 0, 'f', 2).arg(kf1.time, 0, 'f', 2)
                    .arg(natural.duration(), 0, 'f', 2).arg(duration, 0, 'f', 2);
                return false;
            }
        }
        return true;
    }

private:
    double m_maxVelocity = 200.0;     // mm/s
    double m_maxAcceleration = 400.0; // mm/s^2
    GantryLimits m_range;
};

} // namespace timeline
