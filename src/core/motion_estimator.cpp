#include "core/motion_estimator.h"
#include "math/motion_profile.h"
#include <cmath>

namespace motion {

namespace {
/// Axis units covered by one output revolution. Rotary axes have no
/// leadscrew/pulley — one output revolution is simply 360 degrees, so
/// mmPerRev doesn't participate.
double unitsPerOutputRev(const GantryMotorSpec& spec)
{
    return (spec.axisType == GantryAxisType::Rotary) ? 360.0 : spec.mmPerRev;
}
} // namespace

double deriveStepsPerUnit(const GantryMotorSpec& spec)
{
    if (spec.gearRatio <= 0.0) return 0.0;
    const double unitsPerRev = unitsPerOutputRev(spec);
    if (unitsPerRev <= 0.0) return 0.0;
    // pulsesPerRev is per MOTOR rev, so the reducer multiplies the count seen
    // per output rev.
    return spec.pulsesPerRev * spec.gearRatio / unitsPerRev;
}

double deriveStepCeilingVelocityUnitsPerSec(const GantryMotorSpec& spec)
{
    const double stepsPerUnit = deriveStepsPerUnit(spec);
    if (stepsPerUnit <= 0.0 || spec.stepRateCeilingHz <= 0.0) return 0.0;
    return spec.stepRateCeilingHz / stepsPerUnit;
}

double deriveMaxGantryVelocityUnitsPerSec(const GantryMotorSpec& spec)
{
    if (spec.gearRatio <= 0.0) return 0.0;
    double outputRpm = spec.motorRpm / spec.gearRatio;
    double vFromRpm = outputRpm * unitsPerOutputRev(spec) / 60.0;

    // A stepper cannot exceed what the board can clock out, whatever the motor
    // is rated for. Taking the smaller keeps derived segment times honest.
    const double vFromSteps = deriveStepCeilingVelocityUnitsPerSec(spec);
    if (vFromSteps > 0.0 && vFromSteps < vFromRpm) return vFromSteps;
    return vFromRpm;
}

bool stepCeilingIsBinding(const GantryMotorSpec& spec)
{
    if (spec.gearRatio <= 0.0) return false;
    const double vFromSteps = deriveStepCeilingVelocityUnitsPerSec(spec);
    if (vFromSteps <= 0.0) return false;
    const double vFromRpm = (spec.motorRpm / spec.gearRatio) * unitsPerOutputRev(spec) / 60.0;
    return vFromSteps < vFromRpm;
}

QString unitSuffix(const GantryMotorSpec& spec)
{
    return spec.axisType == GantryAxisType::Rotary ? QStringLiteral("°") : QStringLiteral(" mm");
}

QString unitLabel(const GantryMotorSpec& spec)
{
    return spec.axisType == GantryAxisType::Rotary ? QStringLiteral("deg") : QStringLiteral("mm");
}

QString velocityLabel(const GantryMotorSpec& spec)
{
    return spec.axisType == GantryAxisType::Rotary ? QStringLiteral("°/s") : QStringLiteral("mm/s");
}

QString accelLabel(const GantryMotorSpec& spec)
{
    return spec.axisType == GantryAxisType::Rotary ? QStringLiteral("°/s²") : QStringLiteral("mm/s²");
}

double minGantryDurationForDistanceSec(double distanceUnits,
                                        const GantryMotorSpec& spec, double fallbackSec)
{
    if (!spec.configured) return fallbackSec;

    double vMax = deriveMaxGantryVelocityUnitsPerSec(spec);
    if (vMax <= 0.0) return fallbackSec; // invalid spec — don't block the user

    double aMax = spec.maxAccelMmPerSec2 > 0.0 ? spec.maxAccelMmPerSec2 : 1.0;
    double distance = std::abs(distanceUnits);
    if (distance < 1e-6) return 0.0; // no move at all — no artificial floor

    math::TrapezoidalProfile profile(0.0, distance, vMax, aMax);
    return profile.duration();
}

double minGantryDurationSec(const CameraPoint& fromPt, const CameraPoint& toPt,
                             const GantryMotorSpec& spec, double fallbackSec)
{
    return minGantryDurationForDistanceSec(
        toPt.gantryPositionMm - fromPt.gantryPositionMm, spec, fallbackSec);
}

} // namespace motion
