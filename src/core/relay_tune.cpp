#include "core/relay_tune.h"
#include <cmath>
#include <algorithm>

namespace tuning {

namespace {

constexpr int    kDiscardCycles       = 2;    // startup transient
constexpr int    kMinUsableCycles     = 4;
constexpr double kMaxPeriodSpreadPct  = 20.0; // above this, call it non-convergent
constexpr double kMinAmplitude        = 1e-6;
// MSVC only defines M_PI with _USE_MATH_DEFINES; spell it out rather than
// depend on which headers happen to have been pulled in.
constexpr double kPi                  = 3.14159265358979323846;

} // namespace

void applyTuneRule(double ku, double tu, TuneRule rule,
                   double& outKp, double& outKi, double& outKd)
{
    if (ku <= 0.0 || tu <= 0.0) {
        outKp = outKi = outKd = 0.0;
        return;
    }

    double ti = 0.0, td = 0.0;
    switch (rule) {
    case TuneRule::TyreusLuyben:
        outKp = 0.45 * ku;
        ti    = 2.2 * tu;
        td    = tu / 6.3;
        break;
    case TuneRule::ZieglerNichols:
        outKp = 0.60 * ku;
        ti    = tu / 2.0;
        td    = tu / 8.0;
        break;
    }

    // Parallel form: Ki = Kp/Ti, Kd = Kp*Td
    outKi = outKp / ti;
    outKd = outKp * td;
}

RelayResult analyzeRelayOscillation(const QVector<StepSample>& samples,
                                    double relayAmplitudePwm,
                                    double centreUnits,
                                    TuneRule rule)
{
    RelayResult r;

    if (samples.size() < 20) {
        r.message = "not enough samples to identify an oscillation";
        return r;
    }
    if (relayAmplitudePwm <= 0.0) {
        r.message = "relay amplitude must be positive";
        return r;
    }

    // ─── Rising zero-crossings of (measured - centre) ─────────────────────
    // Each consecutive pair of rising crossings bounds one full cycle. Linear
    // interpolation between the straddling samples gives sub-tick timing,
    // which matters at 50Hz against periods of only a few hundred ms.
    QVector<double> crossingTimes;
    for (int i = 1; i < samples.size(); ++i) {
        double prev = samples[i - 1].measured - centreUnits;
        double curr = samples[i].measured - centreUnits;
        if (prev < 0.0 && curr >= 0.0) {
            double span = curr - prev;
            double frac = span > 1e-12 ? (-prev / span) : 0.0;
            crossingTimes.append(samples[i - 1].t
                                 + frac * (samples[i].t - samples[i - 1].t));
        }
    }

    // Cycles = gaps between crossings. Drop the startup transient.
    int totalCycles = crossingTimes.size() - 1;
    if (totalCycles < kDiscardCycles + kMinUsableCycles) {
        r.message = QString("only %1 oscillation cycles captured — need at least %2 "
                            "(try a longer run or a larger relay amplitude)")
                        .arg(qMax(0, totalCycles))
                        .arg(kDiscardCycles + kMinUsableCycles);
        return r;
    }

    QVector<double> periods;
    for (int i = kDiscardCycles; i < crossingTimes.size() - 1; ++i) {
        periods.append(crossingTimes[i + 1] - crossingTimes[i]);
    }
    r.cyclesUsed = periods.size();

    double periodSum = 0.0;
    double periodMin = periods.first();
    double periodMax = periods.first();
    for (double p : periods) {
        periodSum += p;
        periodMin = std::min(periodMin, p);
        periodMax = std::max(periodMax, p);
    }
    r.tu = periodSum / periods.size();

    if (r.tu <= 0.0) {
        r.message = "degenerate oscillation period";
        return r;
    }

    r.periodSpreadPercent = (periodMax - periodMin) / r.tu * 100.0;

    // ─── Amplitude: mean half peak-to-peak over the usable window ─────────
    // Measured per-cycle rather than globally, so one outlier excursion can't
    // inflate Ku for the whole run.
    double windowStart = crossingTimes[kDiscardCycles];
    double windowEnd   = crossingTimes.last();
    double sumHalfPkPk = 0.0;
    int    cyclesMeasured = 0;

    for (int c = kDiscardCycles; c < crossingTimes.size() - 1; ++c) {
        double t0 = crossingTimes[c], t1 = crossingTimes[c + 1];
        bool any = false;
        double lo = 0.0, hi = 0.0;
        for (const auto& s : samples) {
            if (s.t < t0 || s.t > t1) continue;
            double v = s.measured - centreUnits;
            if (!any) { lo = hi = v; any = true; }
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        if (any) {
            sumHalfPkPk += (hi - lo) / 2.0;
            ++cyclesMeasured;
        }
    }
    Q_UNUSED(windowStart) Q_UNUSED(windowEnd)

    if (cyclesMeasured == 0) {
        r.message = "could not measure oscillation amplitude";
        return r;
    }
    r.amplitudeUnits = sumHalfPkPk / cyclesMeasured;

    if (r.amplitudeUnits < kMinAmplitude) {
        r.message = "oscillation amplitude is effectively zero — "
                    "try a larger relay amplitude";
        return r;
    }

    // Convergence gate: a drifting or decaying oscillation gives a Ku that
    // looks plausible but isn't, so refuse rather than hand back bad gains.
    if (r.periodSpreadPercent > kMaxPeriodSpreadPct) {
        r.message = QString("oscillation did not converge — period varied by %1%% "
                            "across %2 cycles (try a larger relay amplitude)")
                        .arg(r.periodSpreadPercent, 0, 'f', 0)
                        .arg(r.cyclesUsed);
        return r;
    }

    // Describing-function result for an ideal relay.
    r.ku = (4.0 * relayAmplitudePwm) / (kPi * r.amplitudeUnits);
    applyTuneRule(r.ku, r.tu, rule, r.kp, r.ki, r.kd);

    r.ok = true;
    r.message = QString("Ku=%1, Tu=%2s from %3 cycles (period spread %4%%)")
                    .arg(r.ku, 0, 'f', 3)
                    .arg(r.tu, 0, 'f', 3)
                    .arg(r.cyclesUsed)
                    .arg(r.periodSpreadPercent, 0, 'f', 1);
    return r;
}

} // namespace tuning
