#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Relay Feedback Auto-Tune (Åström–Hägglund)
//
// Drives the axis into a controlled bang-bang oscillation about a setpoint,
// then reads the ultimate gain Ku and ultimate period Tu straight off that
// oscillation and maps them to PID gains.
//
// Chosen over step-fitting or model identification because it needs no plant
// model, and — critically for a camera crane — the excursion is bounded by
// construction rather than by hoping the model was right.
//
// Pure analysis: no Qt widgets, no threading, no hardware. The relay loop
// itself lives in GantryAxisController; this file only interprets the trace
// it produces, so the algorithm is unit-testable headlessly.
// ═══════════════════════════════════════════════════════════════════════════════

#include "core/step_response_metrics.h"   // StepSample
#include <QVector>
#include <QString>

namespace tuning {

enum class TuneRule {
    /// Gentler, well damped. Default: on a camera axis, overshoot is visible
    /// in the shot, so it is the least acceptable failure mode.
    TyreusLuyben,
    /// Classic quarter-amplitude decay — faster but rings, ~25% overshoot.
    ZieglerNichols
};

struct RelayResult {
    bool    ok = false;
    double  ku = 0.0;   // ultimate gain, PWM per axis unit
    double  tu = 0.0;   // ultimate period, seconds
    double  kp = 0.0;
    double  ki = 0.0;
    double  kd = 0.0;
    int     cyclesUsed = 0;
    double  periodSpreadPercent = 0.0;  // robust (p10-p90) convergence quality
    double  amplitudeUnits = 0.0;       // median half peak-to-peak of the oscillation
    double  samplesPerCycle = 0.0;      // measurement resolution; < ~8 is coarse
    QString message;
};

/// Maps a measured Ku/Tu to PID gains under the chosen rule.
///
/// gantry::PIDController is parallel form (Kp*e + Ki*integral + Kd*deriv), so
/// the classic Ti/Td results are converted: Ki = Kp/Ti, Kd = Kp*Td.
///   Tyreus–Luyben:    Kp = 0.45*Ku,  Ti = 2.2*Tu,   Td = Tu/6.3
///   Ziegler–Nichols:  Kp = 0.60*Ku,  Ti = Tu/2,     Td = Tu/8
void applyTuneRule(double ku, double tu, TuneRule rule,
                   double& outKp, double& outKi, double& outKd);

/// Extracts Ku and Tu from a captured relay oscillation and maps them to
/// gains.
///
/// Procedure, fixed here so it isn't re-derived: find hysteretic crossings of
/// (measured - centre), discard the first two cycles as startup transient,
/// require at least four clean cycles after that, then take Tu as the mean
/// period and `a` as the mean half peak-to-peak amplitude. The describing
/// function for an ideal relay of amplitude d gives Ku = 4d / (pi * a).
///
/// Returns ok == false with an explanatory message rather than emitting
/// plausible-looking garbage when the oscillation never converged — judged by
/// the spread between the longest and shortest measured period.
///
/// @param samples             captured trace; only .t and .measured are read
/// @param relayAmplitudePwm   the +/- PWM the relay switched between (d)
/// @param centreUnits         the position the relay oscillated about
RelayResult analyzeRelayOscillation(const QVector<StepSample>& samples,
                                    double relayAmplitudePwm,
                                    double centreUnits,
                                    TuneRule rule = TuneRule::TyreusLuyben);

} // namespace tuning
