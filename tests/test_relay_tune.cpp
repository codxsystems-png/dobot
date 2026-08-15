// ═══════════════════════════════════════════════════════════════════════════════
// Test: tuning::analyzeRelayOscillation — Ku/Tu extraction from a relay
// oscillation, the TL/ZN gain mappings, and the convergence gate that must
// refuse to emit gains from a trace that never settled into a limit cycle.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "core/relay_tune.h"
#include <cmath>

using namespace tuning;

namespace {

constexpr double kDt = 0.02; // 50Hz control tick

/// Clean sinusoidal limit cycle of known period and amplitude — the ideal
/// relay oscillation the analyser is meant to characterise.
QVector<StepSample> makeLimitCycle(double periodSec, double amplitude,
                                    double centre, double durationSec)
{
    QVector<StepSample> out;
    for (double t = 0.0; t <= durationSec; t += kDt) {
        StepSample s;
        s.t = t;
        s.setpoint = centre;
        s.measured = centre + amplitude * std::sin(2.0 * M_PI * t / periodSec);
        out.append(s);
    }
    return out;
}

} // namespace

class TestRelayTune : public QObject
{
    Q_OBJECT
private slots:
    void testUltimatePeriodIsRecovered()
    {
        const double period = 0.40;
        auto trace = makeLimitCycle(period, /*amplitude=*/5.0, /*centre=*/50.0,
                                    /*durationSec=*/6.0);

        RelayResult r = analyzeRelayOscillation(trace, /*relayAmplitudePwm=*/60.0,
                                                /*centreUnits=*/50.0);

        QVERIFY2(r.ok, qPrintable(r.message));
        QVERIFY2(std::abs(r.tu - period) < 0.02,
                 qPrintable(QString("expected Tu ~%1s, got %2s").arg(period).arg(r.tu)));
        QVERIFY(r.cyclesUsed >= 4);
    }

    void testUltimateGainMatchesDescribingFunction()
    {
        const double amplitude = 5.0;
        const double relayPwm  = 60.0;
        auto trace = makeLimitCycle(0.40, amplitude, 0.0, 6.0);

        RelayResult r = analyzeRelayOscillation(trace, relayPwm, 0.0);
        QVERIFY2(r.ok, qPrintable(r.message));

        // Ku = 4d / (pi * a)
        double expectedKu = (4.0 * relayPwm) / (M_PI * amplitude);
        QVERIFY2(std::abs(r.ku - expectedKu) / expectedKu < 0.05,
                 qPrintable(QString("expected Ku ~%1, got %2").arg(expectedKu).arg(r.ku)));
    }

    void testTyreusLuybenIsGentlerThanZieglerNichols()
    {
        // The justification for defaulting to TL on a camera axis: it must
        // produce a less aggressive controller than ZN for the same plant.
        const double ku = 10.0, tu = 0.5;
        double tlKp, tlKi, tlKd, znKp, znKi, znKd;
        applyTuneRule(ku, tu, TuneRule::TyreusLuyben, tlKp, tlKi, tlKd);
        applyTuneRule(ku, tu, TuneRule::ZieglerNichols, znKp, znKi, znKd);

        QVERIFY2(tlKp < znKp, "TL proportional gain must be lower than ZN");
        QVERIFY2(tlKi < znKi, "TL integral action must be weaker than ZN");
    }

    void testGainsFollowParallelFormConversion()
    {
        const double ku = 10.0, tu = 0.5;
        double kp, ki, kd;
        applyTuneRule(ku, tu, TuneRule::TyreusLuyben, kp, ki, kd);

        // TL: Kp = 0.45*Ku, Ti = 2.2*Tu, Td = Tu/6.3; parallel form
        // converts Ki = Kp/Ti and Kd = Kp*Td.
        const double expectedKp = 0.45 * ku;
        const double expectedKi = expectedKp / (2.2 * tu);
        const double expectedKd = expectedKp * (tu / 6.3);

        QVERIFY(std::abs(kp - expectedKp) < 1e-9);
        QVERIFY(std::abs(ki - expectedKi) < 1e-9);
        QVERIFY(std::abs(kd - expectedKd) < 1e-9);
    }

    void testNonConvergentTraceIsRejected()
    {
        // A chirp: the period keeps changing, so there is no limit cycle and
        // any Ku derived from it would be meaningless.
        QVector<StepSample> trace;
        double phase = 0.0;
        for (double t = 0.0; t <= 6.0; t += kDt) {
            double freq = 1.0 + t * 1.5; // Hz, rising
            phase += 2.0 * M_PI * freq * kDt;
            StepSample s;
            s.t = t;
            s.setpoint = 0.0;
            s.measured = 5.0 * std::sin(phase);
            trace.append(s);
        }

        RelayResult r = analyzeRelayOscillation(trace, 60.0, 0.0);
        QVERIFY2(!r.ok, "a chirp must not be accepted as a converged limit cycle");
        QVERIFY(!r.message.isEmpty());
        QVERIFY2(r.message.contains("converge") || r.message.contains("cycles"),
                 qPrintable("unhelpful rejection message: " + r.message));
    }

    void testTooFewCyclesIsRejected()
    {
        // Only ~2 cycles: below the discard-plus-minimum requirement.
        auto trace = makeLimitCycle(0.5, 5.0, 0.0, 1.0);
        RelayResult r = analyzeRelayOscillation(trace, 60.0, 0.0);
        QVERIFY(!r.ok);
        QVERIFY2(r.message.contains("cycles"),
                 qPrintable("expected a cycle-count complaint, got: " + r.message));
    }

    void testFlatTraceIsRejected()
    {
        QVector<StepSample> flat;
        for (double t = 0.0; t <= 5.0; t += kDt) {
            StepSample s; s.t = t; s.setpoint = 0.0; s.measured = 0.0;
            flat.append(s);
        }
        RelayResult r = analyzeRelayOscillation(flat, 60.0, 0.0);
        QVERIFY2(!r.ok, "a motionless axis must not yield tuning gains");
    }

    void testZeroRelayAmplitudeIsRejected()
    {
        auto trace = makeLimitCycle(0.4, 5.0, 0.0, 6.0);
        RelayResult r = analyzeRelayOscillation(trace, 0.0, 0.0);
        QVERIFY(!r.ok);
        QVERIFY(r.message.contains("amplitude"));
    }

    void testOscillationAboutNonZeroCentreIsHandled()
    {
        // The relay runs about the middle of travel, not about zero.
        const double centre = 250.0;
        auto trace = makeLimitCycle(0.4, 5.0, centre, 6.0);

        RelayResult r = analyzeRelayOscillation(trace, 60.0, centre);
        QVERIFY2(r.ok, qPrintable(r.message));
        QVERIFY2(std::abs(r.amplitudeUnits - 5.0) < 0.5,
                 qPrintable(QString("expected amplitude ~5, got %1").arg(r.amplitudeUnits)));
    }

    void testLargerAmplitudeGivesLowerUltimateGain()
    {
        // Ku is inversely proportional to the oscillation amplitude, so a
        // floppier plant (bigger swing for the same relay drive) must tune
        // to a gentler controller.
        auto small = makeLimitCycle(0.4, 2.0, 0.0, 6.0);
        auto large = makeLimitCycle(0.4, 8.0, 0.0, 6.0);

        RelayResult rSmall = analyzeRelayOscillation(small, 60.0, 0.0);
        RelayResult rLarge = analyzeRelayOscillation(large, 60.0, 0.0);

        QVERIFY(rSmall.ok && rLarge.ok);
        QVERIFY2(rLarge.ku < rSmall.ku,
                 "a larger oscillation amplitude must yield a lower Ku");
        QVERIFY2(rLarge.kp < rSmall.kp, "and therefore a lower Kp");
    }
};

QTEST_APPLESS_MAIN(TestRelayTune)
#include "test_relay_tune.moc"
