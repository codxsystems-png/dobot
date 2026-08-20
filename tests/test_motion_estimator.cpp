// ═══════════════════════════════════════════════════════════════════════════════
// Test: motion::deriveMaxGantryVelocityUnitsPerSec / minGantryDurationSec
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
        // Every axis is a stepper now, so the step-rate ceiling always
        // participates. These cases exercise the RPM path specifically, so
        // the ceiling is set high enough not to bind — the ceiling has its
        // own tests below.
        spec.stepRateCeilingHz = 1.0e9;
        // outputRpm = 3000/10 = 300; maxVel = 300*5/60 = 25.0 mm/s
        QCOMPARE(motion::deriveMaxGantryVelocityUnitsPerSec(spec), 25.0);
    }

    void testDeriveVelocity_RotaryUses360DegreesPerRevIgnoringMmPerRev()
    {
        GantryMotorSpec spec;
        spec.motorRpm = 600.0;
        spec.gearRatio = 10.0;
        spec.mmPerRev = 999.0;  // must be ignored entirely in Rotary mode
        spec.axisType = GantryAxisType::Rotary;
        // Every axis is a stepper now, so the step-rate ceiling always
        // participates. These cases exercise the RPM path specifically, so
        // the ceiling is set high enough not to bind — the ceiling has its
        // own tests below.
        spec.stepRateCeilingHz = 1.0e9;
        // outputRpm = 600/10 = 60 rev/min = 1 rev/s = 360 deg/s
        QCOMPARE(motion::deriveMaxGantryVelocityUnitsPerSec(spec), 360.0);
    }

    void testUnitLabelsFollowAxisType()
    {
        GantryMotorSpec linear;   // Linear is the default
        QCOMPARE(motion::unitLabel(linear), QStringLiteral("mm"));
        QCOMPARE(motion::velocityLabel(linear), QStringLiteral("mm/s"));

        GantryMotorSpec rotary;
        rotary.axisType = GantryAxisType::Rotary;
        QCOMPARE(motion::unitLabel(rotary), QStringLiteral("deg"));
        QCOMPARE(motion::velocityLabel(rotary), QStringLiteral("°/s"));
    }

    void testDeriveVelocity_ZeroOrNegativeGearRatioReturnsZero()
    {
        GantryMotorSpec spec;
        spec.gearRatio = 0.0;
        QCOMPARE(motion::deriveMaxGantryVelocityUnitsPerSec(spec), 0.0);

        spec.gearRatio = -1.0;
        QCOMPARE(motion::deriveMaxGantryVelocityUnitsPerSec(spec), 0.0);
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
        // Every axis is a stepper now, so the step-rate ceiling always
        // participates. These cases exercise the RPM path specifically, so
        // the ceiling is set high enough not to bind — the ceiling has its
        // own tests below.
        spec.stepRateCeilingHz = 1.0e9;


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

    // ─── Stepper: the pulse ceiling usually binds before motor RPM ────────

    void testStepsPerUnitFromPulsesGearAndPitch()
    {
        GantryMotorSpec spec;
        spec.pulsesPerRev = 1600.0;
        spec.gearRatio    = 1.0;
        spec.mmPerRev     = 4.0;          // 4mm leadscrew

        QCOMPARE(motion::deriveStepsPerUnit(spec), 400.0);
    }

    void testStepsPerUnitRotaryUses360()
    {
        GantryMotorSpec spec;
        spec.axisType     = GantryAxisType::Rotary;
        spec.pulsesPerRev = 1800.0;
        spec.gearRatio    = 2.0;          // 2:1 reducer

        // 1800 pulses per motor rev x 2 motor revs per output rev / 360 deg
        QCOMPARE(motion::deriveStepsPerUnit(spec), 10.0);
    }

    /// The case that matters: a motor rated for 200mm/s on a board that can
    /// only clock 20mm/s worth of pulses. Deriving from RPM alone would put
    /// segment times on the timeline that the axis silently cannot meet.
    void testStepCeilingCapsVelocityBelowRpm()
    {
        GantryMotorSpec spec;
        spec.motorRpm          = 3000.0;
        spec.gearRatio         = 1.0;
        spec.mmPerRev          = 4.0;
        spec.pulsesPerRev      = 1600.0;
        spec.stepRateCeilingHz = 8000.0;

        // RPM path would give 3000/60 * 4 = 200 mm/s.
        // Step path gives 8000 / 400 = 20 mm/s, and that is what binds.
        QCOMPARE(motion::deriveMaxGantryVelocityUnitsPerSec(spec), 20.0);
        QVERIFY(motion::stepCeilingIsBinding(spec));
    }

    void testRpmStillBindsWhenCeilingIsGenerous()
    {
        GantryMotorSpec spec;
        spec.motorRpm          = 300.0;
        spec.gearRatio         = 1.0;
        spec.mmPerRev          = 4.0;
        spec.pulsesPerRev      = 1600.0;
        spec.stepRateCeilingHz = 100000.0;   // far more than the motor can use

        QCOMPARE(motion::deriveMaxGantryVelocityUnitsPerSec(spec), 20.0); // 300/60*4
        QVERIFY(!motion::stepCeilingIsBinding(spec));
    }

};

QTEST_MAIN(TestMotionEstimator)
#include "test_motion_estimator.moc"
