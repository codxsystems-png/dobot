// ═══════════════════════════════════════════════════════════════════════════════
// Test: tuning::computeStepMetrics — overshoot, settling time, rise time and
// steady-state error over synthetic step traces. Pure function, no hardware.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "core/step_response_metrics.h"
#include <cmath>

using namespace tuning;

namespace {

constexpr double kDt = 0.02; // 50Hz, matching the real control tick

/// Underdamped second-order step response with a known overshoot, built from
/// the closed-form solution so the expected peak is analytic rather than
/// simulated: y(t) = A * (1 - e^(-z*wn*t)/sqrt(1-z^2) * sin(wd*t + phi))
/// Overshoot for a given damping ratio z is exp(-pi*z/sqrt(1-z^2)).
QVector<StepSample> makeSecondOrderTrace(double amplitude, double zeta, double wn,
                                          double durationSec, int pwm = 0)
{
    QVector<StepSample> out;
    const double wd  = wn * std::sqrt(1.0 - zeta * zeta);
    const double phi = std::acos(zeta);
    for (double t = 0.0; t <= durationSec; t += kDt) {
        double y = amplitude * (1.0 - std::exp(-zeta * wn * t) / std::sqrt(1.0 - zeta * zeta)
                                        * std::sin(wd * t + phi));
        StepSample s;
        s.t = t;
        s.setpoint = amplitude;
        s.measured = y;
        s.pwm = pwm;
        out.append(s);
    }
    return out;
}

/// Monotonic first-order approach — no overshoot at all.
QVector<StepSample> makeFirstOrderTrace(double amplitude, double tau, double durationSec)
{
    QVector<StepSample> out;
    for (double t = 0.0; t <= durationSec; t += kDt) {
        StepSample s;
        s.t = t;
        s.setpoint = amplitude;
        s.measured = amplitude * (1.0 - std::exp(-t / tau));
        out.append(s);
    }
    return out;
}

} // namespace

class TestStepResponseMetrics : public QObject
{
    Q_OBJECT
private slots:
    void testTooFewSamplesIsInvalid()
    {
        QVector<StepSample> few;
        for (int i = 0; i < 5; ++i) {
            StepSample s; s.t = i * kDt; s.setpoint = 10.0; s.measured = i;
            few.append(s);
        }
        StepMetrics m = computeStepMetrics(few);
        QVERIFY(!m.valid);
        QVERIFY(m.note.contains("at least"));
    }

    void testZeroAmplitudeIsInvalid()
    {
        QVector<StepSample> flat;
        for (int i = 0; i < 50; ++i) {
            StepSample s; s.t = i * kDt; s.setpoint = 5.0; s.measured = 5.0;
            flat.append(s);
        }
        StepMetrics m = computeStepMetrics(flat);
        QVERIFY(!m.valid);
        QVERIFY(m.note.contains("amplitude"));
    }

    void testKnownOvershootIsRecovered()
    {
        // zeta = 0.456 gives ~20% overshoot: exp(-pi*z/sqrt(1-z^2)) ~= 0.20
        const double zeta = 0.456;
        const double expectedOvershoot =
            std::exp(-M_PI * zeta / std::sqrt(1.0 - zeta * zeta)) * 100.0;

        auto trace = makeSecondOrderTrace(/*amplitude=*/100.0, zeta, /*wn=*/20.0,
                                          /*durationSec=*/3.0);
        StepMetrics m = computeStepMetrics(trace);

        QVERIFY(m.valid);
        // Sampling at 50Hz means the true peak falls between samples, so allow
        // a small tolerance rather than demanding the analytic value exactly.
        QVERIFY2(std::abs(m.overshootPercent - expectedOvershoot) < 2.0,
                 qPrintable(QString("expected ~%1%% overshoot, got %2%%")
                                .arg(expectedOvershoot).arg(m.overshootPercent)));
    }

    void testCriticallyDampedTraceHasNoOvershoot()
    {
        auto trace = makeFirstOrderTrace(/*amplitude=*/50.0, /*tau=*/0.2, /*durationSec=*/3.0);
        StepMetrics m = computeStepMetrics(trace);

        QVERIFY(m.valid);
        QCOMPARE(m.overshootPercent, 0.0);
    }

    void testRiseTimeIsPositiveAndOrdered()
    {
        auto fast = makeFirstOrderTrace(100.0, 0.1, 3.0);
        auto slow = makeFirstOrderTrace(100.0, 0.5, 5.0);

        StepMetrics mFast = computeStepMetrics(fast);
        StepMetrics mSlow = computeStepMetrics(slow);

        QVERIFY(mFast.valid && mSlow.valid);
        QVERIFY(mFast.riseTimeSec > 0.0);
        QVERIFY2(mFast.riseTimeSec < mSlow.riseTimeSec,
                 "a faster time constant must produce a shorter rise time");
    }

    void testSteadyStateErrorIsMeasured()
    {
        // Settles to 95 against a setpoint of 100 -> 5 units of offset.
        QVector<StepSample> trace;
        for (int i = 0; i < 100; ++i) {
            StepSample s;
            s.t = i * kDt;
            s.setpoint = 100.0;
            s.measured = 95.0 * (1.0 - std::exp(-(i * kDt) / 0.2));
            trace.append(s);
        }
        StepMetrics m = computeStepMetrics(trace);

        QVERIFY(m.valid);
        QVERIFY2(std::abs(m.steadyStateError - 5.0) < 0.5,
                 qPrintable(QString("expected ~5 units of offset, got %1").arg(m.steadyStateError)));
    }

    void testNeverSettlingTraceIsFlagged()
    {
        // Sustained oscillation that never enters the band.
        QVector<StepSample> trace;
        for (int i = 0; i < 200; ++i) {
            double t = i * kDt;
            StepSample s;
            s.t = t;
            s.setpoint = 100.0;
            s.measured = 100.0 + 30.0 * std::sin(2.0 * M_PI * 2.0 * t);
            trace.append(s);
        }
        // Start away from the setpoint so there's a real step amplitude.
        trace.first().measured = 0.0;

        StepMetrics m = computeStepMetrics(trace);
        QVERIFY(m.valid);
        QVERIFY2(m.note.contains("did not settle"),
                 qPrintable("expected a did-not-settle note, got: " + m.note));
    }

    void testHeavyStaleFractionIsFlaggedButStillValid()
    {
        auto trace = makeFirstOrderTrace(100.0, 0.2, 3.0);
        for (int i = 0; i < trace.size(); ++i) {
            if (i % 2 == 0) trace[i].stale = true; // 50% stale
        }
        StepMetrics m = computeStepMetrics(trace);

        QVERIFY2(m.valid, "a stale-heavy trace is still computable");
        QVERIFY2(m.note.contains("stale"),
                 qPrintable("expected a dropout warning, got: " + m.note));
    }

    void testSaturatedSamplesAreCounted()
    {
        auto trace = makeFirstOrderTrace(100.0, 0.2, 2.0);
        for (int i = 0; i < trace.size(); ++i) {
            trace[i].pwm = (i < 20) ? 255 : 100;
        }
        StepMetrics m = computeStepMetrics(trace);

        QVERIFY(m.valid);
        QCOMPARE(m.saturatedSamples, 20);
    }

    void testNegativeStepIsHandledSymmetrically()
    {
        // Same shape, mirrored: starts at 100, steps down to 0.
        auto rising = makeFirstOrderTrace(100.0, 0.2, 3.0);
        QVector<StepSample> falling;
        for (const auto& s : rising) {
            StepSample f = s;
            f.setpoint = 0.0;
            f.measured = 100.0 - s.measured;
            falling.append(f);
        }

        StepMetrics up   = computeStepMetrics(rising);
        StepMetrics down = computeStepMetrics(falling);

        QVERIFY(up.valid && down.valid);
        QVERIFY2(std::abs(up.stepAmplitude - down.stepAmplitude) < 1e-9,
                 "a downward step must report the same amplitude as its mirror");
        QVERIFY2(std::abs(up.riseTimeSec - down.riseTimeSec) < 1e-9,
                 "a downward step must report the same rise time as its mirror");
        QCOMPARE(down.overshootPercent, 0.0);
    }
};

QTEST_APPLESS_MAIN(TestStepResponseMetrics)
#include "test_step_response_metrics.moc"
