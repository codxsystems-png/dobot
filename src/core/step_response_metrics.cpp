#include "core/step_response_metrics.h"
#include <cmath>
#include <algorithm>

namespace tuning {

namespace {

constexpr int    kMinSamples          = 10;
constexpr double kMinAmplitude        = 1e-6;
constexpr double kFinalValueFraction  = 0.20; // mean of the trailing 20%
constexpr double kStaleFractionWarn   = 0.30;
constexpr int    kPwmSaturation       = 255;

} // namespace

StepMetrics computeStepMetrics(const QVector<StepSample>& samples,
                               double settlingBandPercent)
{
    StepMetrics m;

    if (samples.size() < kMinSamples) {
        m.note = QString("only %1 samples — need at least %2")
                     .arg(samples.size()).arg(kMinSamples);
        return m;
    }

    const double setpoint = samples.last().setpoint;
    const double start    = samples.first().measured;
    const double signedAmplitude = setpoint - start;
    m.stepAmplitude = std::abs(signedAmplitude);

    if (m.stepAmplitude < kMinAmplitude) {
        m.note = "step amplitude is zero — nothing to measure";
        return m;
    }

    const double dir = signedAmplitude > 0.0 ? 1.0 : -1.0;

    // ─── Final value: mean of the trailing portion ────────────────────────
    int tailCount = std::max(1, static_cast<int>(samples.size() * kFinalValueFraction));
    double tailSum = 0.0;
    for (int i = samples.size() - tailCount; i < samples.size(); ++i) {
        tailSum += samples[i].measured;
    }
    m.finalValue = tailSum / tailCount;
    m.steadyStateError = setpoint - m.finalValue;

    // ─── Peak (furthest travel in the direction of the step) ──────────────
    m.peakValue = start;
    int peakIndex = 0;
    for (int i = 0; i < samples.size(); ++i) {
        if ((samples[i].measured - start) * dir > (m.peakValue - start) * dir) {
            m.peakValue = samples[i].measured;
            peakIndex = i;
        }
    }

    // Overshoot only counts travel PAST the final value, in the step
    // direction. An undershooting (over-damped) response scores zero rather
    // than a negative number.
    //
    // A peak sitting inside the trailing averaging window is NOT overshoot —
    // that's a monotonic response still asymptotically approaching its final
    // value, where the last sample is naturally above the mean of the window.
    // Real overshoot peaks in the interior and then comes back down.
    const int tailStart = samples.size() - tailCount;
    if (peakIndex < tailStart) {
        double beyondFinal = (m.peakValue - m.finalValue) * dir;
        m.overshootPercent = beyondFinal > 0.0 ? (beyondFinal / m.stepAmplitude) * 100.0 : 0.0;
    } else {
        m.overshootPercent = 0.0;
    }

    // ─── Rise time: 10% -> 90% of the step ────────────────────────────────
    const double tenPct    = start + signedAmplitude * 0.10;
    const double ninetyPct = start + signedAmplitude * 0.90;
    double tTen = -1.0, tNinety = -1.0;
    for (const auto& s : samples) {
        if (tTen < 0.0 && (s.measured - tenPct) * dir >= 0.0)       tTen = s.t;
        if (tNinety < 0.0 && (s.measured - ninetyPct) * dir >= 0.0) tNinety = s.t;
        if (tTen >= 0.0 && tNinety >= 0.0) break;
    }
    m.riseTimeSec = (tTen >= 0.0 && tNinety >= tTen) ? (tNinety - tTen) : 0.0;

    // ─── Settling: last exit from the band around the final value ─────────
    const double band = m.stepAmplitude * (settlingBandPercent / 100.0);
    double lastExit = -1.0;
    for (const auto& s : samples) {
        if (std::abs(s.measured - m.finalValue) > band) {
            lastExit = s.t;
        }
    }
    if (lastExit < 0.0) {
        m.settlingTimeSec = 0.0; // never left the band at all
    } else if (lastExit >= samples.last().t) {
        // Still outside the band when the capture ended — it never settled.
        m.settlingTimeSec = samples.last().t;
        m.note = "did not settle within the capture window";
    } else {
        m.settlingTimeSec = lastExit;
    }

    // ─── Diagnostics that explain a bad-looking result ────────────────────
    int staleCount = 0;
    for (const auto& s : samples) {
        if (s.stale) ++staleCount;
        if (std::abs(s.pwm) >= kPwmSaturation) ++m.saturatedSamples;
    }

    m.valid = true;

    double staleFraction = static_cast<double>(staleCount) / samples.size();
    if (staleFraction > kStaleFractionWarn) {
        // Still "valid" — the numbers are computable — but the operator needs
        // to know the trace itself is largely carried-over values.
        QString dropoutNote = QString("%1% of samples were stale (serial dropouts) — "
                                       "result unreliable").arg(staleFraction * 100.0, 0, 'f', 0);
        m.note = m.note.isEmpty() ? dropoutNote : m.note + "; " + dropoutNote;
    }

    return m;
}

} // namespace tuning
