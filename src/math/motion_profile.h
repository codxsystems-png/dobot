#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Motion Profile Generator
// Computes 1D kinematic profiles (trapezoidal) for timeline tracks.
// ═══════════════════════════════════════════════════════════════════════════════

namespace math {

struct ProfileState {
    double position;
    double velocity;
    double acceleration;
};

class TrapezoidalProfile {
public:
    TrapezoidalProfile(double q0, double q1, double vMax, double aMax);

    // Build a profile that takes exactly `syncDuration` seconds, by scaling
    // vMax/aMax down so every axis on a multi-axis move lands together at
    // the same keyframe time. If `syncDuration` is shorter than the fastest
    // this move can physically go within vMax/aMax, the limits win instead
    // and the natural (fastest-possible) profile is returned.
    static TrapezoidalProfile fitToDuration(double q0, double q1, double vMax, double aMax, double syncDuration);

    // Get the exact state at time t (where t=0 is the start of the move)
    ProfileState sampleAt(double t) const;

    // Total duration of the motion profile
    double duration() const { return m_tf; }

private:
    double m_q0;
    double m_q1;
    double m_vMax;
    double m_aMax;
    
    int m_dir; // 1 or -1
    
    double m_ta; // Time to accelerate
    double m_tc; // Time at constant velocity
    double m_td; // Time to decelerate
    double m_tf; // Total time (ta + tc + td)
    
    double m_vLimit; // The actual peak velocity reached
};

} // namespace math
