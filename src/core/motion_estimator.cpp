#include "core/motion_estimator.h"
#include "math/motion_profile.h"
#include <cmath>

namespace motion {

double deriveMaxGantryVelocityUnitsPerSec(const GantryMotorSpec& spec)
{
    if (spec.gearRatio <= 0.0) return 0.0;
    double outputRpm = spec.motorRpm / spec.gearRatio;
    // Rotary axes have no leadscrew/pulley — one output revolution is simply
    // 360 degrees, so mmPerRev doesn't participate.
    double unitsPerRev = (spec.axisType == GantryAxisType::Rotary) ? 360.0 : spec.mmPerRev;
    return outputRpm * unitsPerRev / 60.0;
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

double minGantryDurationSec(const CameraPoint& fromPt, const CameraPoint& toPt,
                             const GantryMotorSpec& spec, double fallbackSec)
{
    if (!spec.configured) return fallbackSec;

    double vMax = deriveMaxGantryVelocityUnitsPerSec(spec);
    if (vMax <= 0.0) return fallbackSec; // invalid spec — don't block the user

    double aMax = spec.maxAccelMmPerSec2 > 0.0 ? spec.maxAccelMmPerSec2 : 1.0;
    double distanceMm = std::abs(toPt.gantryPositionMm - fromPt.gantryPositionMm);
    if (distanceMm < 1e-6) return 0.0; // no gantry move at all — no artificial floor

    math::TrapezoidalProfile profile(0.0, distanceMm, vMax, aMax);
    return profile.duration();
}

} // namespace motion
