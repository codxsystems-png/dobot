#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — PID Controller (Phase 3: gantry closed-loop upgrade)
// Standalone, transport-agnostic PID with clamping anti-windup and
// derivative-on-measurement (avoids a derivative kick when the setpoint
// itself jumps). The caller supplies dtSec each call since this class owns
// no timer of its own — that keeps it trivially unit-testable.
// ═══════════════════════════════════════════════════════════════════════════════

#include <algorithm>

namespace gantry {

class PIDController {
public:
    PIDController(double kp, double ki, double kd, double outputMin, double outputMax)
        : m_kp(kp), m_ki(ki), m_kd(kd), m_outputMin(outputMin), m_outputMax(outputMax)
    {}

    void setGains(double kp, double ki, double kd) {
        m_kp = kp;
        m_ki = ki;
        m_kd = kd;
    }

    void setOutputLimits(double outputMin, double outputMax) {
        m_outputMin = outputMin;
        m_outputMax = outputMax;
    }

    /// Clears accumulated integral/derivative state. Call this whenever the
    /// loop is (re)started after being idle — otherwise a stale integral or
    /// a huge derivative-on-measurement spike (from a large gap since the
    /// last compute()) can throw the first output.
    void reset() {
        m_integral = 0.0;
        m_lastMeasurement = 0.0;
        m_hasLastMeasurement = false;
    }

    /// Compute the next control output.
    /// `error` = setpoint - measurement. `measurement` is the current process
    /// value (used for derivative-on-measurement). `dtSec` is the elapsed
    /// time since the previous compute() call, measured by the caller.
    double compute(double error, double measurement, double dtSec)
    {
        if (dtSec <= 0.0) {
            return std::clamp(m_kp * error, m_outputMin, m_outputMax);
        }

        double proposedIntegral = m_integral + error * dtSec;
        double derivative = m_hasLastMeasurement
            ? -(measurement - m_lastMeasurement) / dtSec
            : 0.0;

        double unclamped = m_kp * error + m_ki * proposedIntegral + m_kd * derivative;

        // Clamping anti-windup: only accumulate the integral while doing so
        // wouldn't just be feeding an output that's already saturated in the
        // direction the error is pushing it. Without this, a sustained large
        // error saturates the output and the integral keeps growing far past
        // what's achievable, so the output stays pegged long after the error
        // resolves (the classic "windup" overshoot).
        bool saturatedHigh = unclamped > m_outputMax;
        bool saturatedLow  = unclamped < m_outputMin;
        bool integralPushesFurther =
            (saturatedHigh && error > 0.0) || (saturatedLow && error < 0.0);

        if (!integralPushesFurther) {
            m_integral = proposedIntegral;
        }

        double output = m_kp * error + m_ki * m_integral + m_kd * derivative;

        m_lastMeasurement = measurement;
        m_hasLastMeasurement = true;

        return std::clamp(output, m_outputMin, m_outputMax);
    }

private:
    double m_kp;
    double m_ki;
    double m_kd;
    double m_outputMin;
    double m_outputMax;

    double m_integral = 0.0;
    double m_lastMeasurement = 0.0;
    bool   m_hasLastMeasurement = false;
};

} // namespace gantry
