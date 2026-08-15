#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Step Response Metrics
// Pure analysis of a captured step response: overshoot, settling time, rise
// time, steady-state error. No Qt widgets and no threading, so the whole
// thing is unit-testable headlessly against synthetic traces.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QVector>
#include <QString>

namespace tuning {

/// One tick of captured telemetry from the axis control loop.
/// `stale` marks a tick where no fresh encoder reading arrived (the serial
/// query timed out), so `measured` is a carried-over value rather than a real
/// measurement — see GantryAxisController's PENDING_TIMEOUT_TICKS.
struct StepSample {
    double t        = 0.0;   // seconds since the step was commanded
    double setpoint = 0.0;   // commanded position, in axis units
    double measured = 0.0;   // encoder position, in axis units
    int    pwm      = 0;     // commanded PWM, signed
    bool   stale    = false;
};

struct StepMetrics {
    bool    valid              = false;
    double  overshootPercent   = 0.0;  // (peak - final) / stepAmplitude * 100
    double  settlingTimeSec    = 0.0;  // last time the trace left the settling band
    double  riseTimeSec        = 0.0;  // 10% -> 90% of step amplitude
    double  steadyStateError   = 0.0;  // setpoint - final (signed)
    double  peakValue          = 0.0;  // extreme measured value in the step direction
    double  finalValue         = 0.0;  // mean of the last 20% of samples
    double  stepAmplitude      = 0.0;  // |setpoint - starting position|
    int     saturatedSamples   = 0;    // ticks at |pwm| >= 255 — explains a bad result
    QString note;                      // why invalid, or why the result is untrustworthy
};

/// Analyses a captured step response.
///
/// Conventions, fixed here so they aren't re-derived at each call site:
///  - Step amplitude is |setpoint - measured[0]|; the capture is expected to
///    begin at the moment the step is commanded.
///  - `finalValue` is the mean of the last 20% of samples, which rejects
///    encoder quantisation noise better than taking the last sample alone.
///  - Overshoot is measured against the ACHIEVED final value, not the
///    setpoint, so a steady-state offset can't masquerade as overshoot.
///  - The settling band is settlingBandPercent% of the step amplitude around
///    the final value (textbook definition). A system that settles to the
///    wrong place still "settles" — read steadyStateError for the offset.
///  - Settling time is the LAST exit from that band, not the first entry, so
///    a late ring-out isn't scored as settled.
///
/// Returns valid == false with an explanatory note when the trace is too
/// short (< 10 samples) or the step amplitude is degenerate.
StepMetrics computeStepMetrics(const QVector<StepSample>& samples,
                               double settlingBandPercent = 2.0);

} // namespace tuning
