#include "math/motion_profile.h"
#include <cmath>

namespace math {

TrapezoidalProfile::TrapezoidalProfile(double q0, double q1, double vMax, double aMax)
    : m_q0(q0), m_q1(q1)
{
    // Ensure positive limits
    m_vMax = std::abs(vMax);
    m_aMax = std::abs(aMax);
    if (m_aMax < 1e-6) m_aMax = 1e-6; // Prevent division by zero

    double h = q1 - q0;
    m_dir = (h > 0) ? 1 : (h < 0 ? -1 : 0);
    h = std::abs(h);

    if (h < 1e-6) {
        // No movement needed
        m_ta = m_tc = m_td = m_tf = 0.0;
        m_vLimit = 0.0;
        return;
    }

    // Time to reach max velocity
    double t_accel = m_vMax / m_aMax;
    // Distance covered to reach max velocity
    double d_accel = 0.5 * m_aMax * t_accel * t_accel;

    if (2.0 * d_accel > h) {
        // Triangular profile (cannot reach vMax)
        m_ta = std::sqrt(h / m_aMax);
        m_tc = 0.0;
        m_td = m_ta;
        m_vLimit = m_aMax * m_ta;
    } else {
        // Trapezoidal profile
        m_ta = t_accel;
        m_td = t_accel;
        m_vLimit = m_vMax;
        // Remaining distance for constant velocity
        double d_const = h - (2.0 * d_accel);
        m_tc = d_const / m_vMax;
    }

    m_tf = m_ta + m_tc + m_td;
}

TrapezoidalProfile TrapezoidalProfile::fitToDuration(double q0, double q1, double vMax, double aMax, double syncDuration)
{
    TrapezoidalProfile natural(q0, q1, vMax, aMax);

    if (syncDuration <= natural.m_tf || natural.m_tf <= 0.0) {
        // Either the keyframe interval already matches/exceeds what's
        // physically possible, or there's no move at all — the natural
        // (fastest, limit-respecting) profile is the right answer.
        return natural;
    }

    // Stretch time by k: scaling vMax -> vMax/k and aMax -> aMax/k^2
    // reproduces the exact same trapezoidal/triangular shape (same
    // acceleration distance, same branch selection) but taking k times
    // as long — so it always lands exactly at syncDuration.
    double k = syncDuration / natural.m_tf;
    return TrapezoidalProfile(q0, q1, vMax / k, aMax / (k * k));
}

ProfileState TrapezoidalProfile::sampleAt(double t) const
{
    if (m_dir == 0 || t <= 0.0) {
        return {m_q0, 0.0, 0.0};
    }

    if (t >= m_tf) {
        return {m_q1, 0.0, 0.0};
    }

    ProfileState state;

    if (t < m_ta) {
        // Acceleration phase
        state.position = m_q0 + m_dir * (0.5 * m_aMax * t * t);
        state.velocity = m_dir * (m_aMax * t);
        state.acceleration = m_dir * m_aMax;
    } else if (t < m_ta + m_tc) {
        // Constant velocity phase
        double dt = t - m_ta;
        double d_accel = 0.5 * m_aMax * m_ta * m_ta;
        state.position = m_q0 + m_dir * (d_accel + m_vLimit * dt);
        state.velocity = m_dir * m_vLimit;
        state.acceleration = 0.0;
    } else {
        // Deceleration phase
        double dt = t - m_ta - m_tc;
        double d_accel = 0.5 * m_aMax * m_ta * m_ta;
        double d_const = m_vLimit * m_tc;
        state.position = m_q0 + m_dir * (d_accel + d_const + m_vLimit * dt - 0.5 * m_aMax * dt * dt);
        state.velocity = m_dir * (m_vLimit - m_aMax * dt);
        state.acceleration = -m_dir * m_aMax;
    }

    return state;
}

} // namespace math
