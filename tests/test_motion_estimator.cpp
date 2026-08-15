// ═══════════════════════════════════════════════════════════════════════════════
// Test: motion::deriveMaxGantryVelocityMmPerSec / minGantryDurationSec
// Pins the RPM/gear-ratio/mm-per-rev conversion formula and the fallback
// behavior an unconfigured or invalid GantryMotorSpec must produce.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include "core/motion_estimator.h"
#include "math/motion_profile.h"

class TestMotionEstimator : public QObject
{
    Q_OBJECT
private slots:
    void testDeriveVelocity_KnownRpmGearRatioMmPerRev()
    {
        GantryMotorSpec spec;
        spec.motorRpm = 3000.0;
        spec.gearRatio = 10.0;
        spec.mmPerRev = 5.0;
        // outputRpm = 3000/10 = 300; maxVel = 300*5/60 = 25.0 mm/s
        QCOMPARE(motion::deriveMaxGantryVelocityMmPerSec(spec), 25.0);
    }

    void testDeriveVelocity_ZeroOrNegativeGearRatioReturnsZero()
    {
        GantryMotorSpec spec;
        spec.gearRatio = 0.0;
        QCOMPARE(motion::deriveMaxGantryVelocityMmPerSec(spec), 0.0);

        spec.gearRatio = -1.0;
        QCOMPARE(motion::deriveMaxGantryVelocityMmPerSec(spec), 0.0);
    }

    void testMinGantryDuration_UnconfiguredSpecReturnsFallback()
    {
        GantryMotorSpec spec; // configured = false by default
        CameraPoint from, to;
        from.id = "a"; from.gantryPositionMm = 0.0;
        to.id = "b";   to.gantryPositionMm = 1000.0;

        QCOMPARE(motion::minGantryDurationSec(from, to, spec, 2.0), 2.0);
    }

    void testMinGantryDuration_ZeroDistanceReturnsZero()
    {
        GantryMotorSpec spec;
        spec.motorRpm = 3000.0;
        spec.gearRatio = 1.0;
        spec.mmPerRev = 8.0;
        spec.configured = true;

        CameraPoint from, to;
        from.id = "a"; from.gantryPositionMm = 42.0;
        to.id = "b";   to.gantryPositionMm = 42.0;

        QCOMPARE(motion::minGantryDurationSec(from, to, spec, 2.0), 0.0);
    }

    void testMinGantryDuration_MatchesTrapezoidalProfileDuration()
    {
        GantryMotorSpec spec;
        spec.motorRpm = 3000.0;
        spec.gearRatio = 10.0;
        spec.mmPerRev = 5.0; // vMax = 25.0 mm/s
        spec.maxAccelMmPerSec2 = 50.0;
        spec.configured = true;

        CameraPoint from, to;
        from.id = "a"; from.gantryPositionMm = 0.0;
        to.id = "b";   to.gantryPositionMm = 300.0;

        double got = motion::minGantryDurationSec(from, to, spec, -1.0);
        math::TrapezoidalProfile expected(0.0, 300.0, 25.0, 50.0);
        QCOMPARE(got, expected.duration());
    }

    void testMinGantryDuration_InvalidConfiguredSpecFallsBackNotCrash()
    {
        GantryMotorSpec spec;
        spec.gearRatio = 0.0; // invalid — would divide by zero if not guarded
        spec.configured = true;

        CameraPoint from, to;
        from.id = "a"; from.gantryPositionMm = 0.0;
        to.id = "b";   to.gantryPositionMm = 100.0;

        QCOMPARE(motion::minGantryDurationSec(from, to, spec, 3.0), 3.0);
    }
};

QTEST_MAIN(TestMotionEstimator)
#include "test_motion_estimator.moc"
