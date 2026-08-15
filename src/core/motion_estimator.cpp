#include "core/motion_estimator.h"
#include "math/motion_profile.h"
#include <cmath>

namespace motion {

double deriveMaxGantryVelocityMmPerSec(const GantryMotorSpec& spec)
{
    if (spec.gearRatio <= 0.0) return 0.0;
    double outputRpm = spec.motorRpm / spec.gearRatio;
    return outputRpm * spec.mmPerRev / 60.0;
}

double minGantryDurationSec(const CameraPoint& fromPt, const CameraPoint& toPt,
                             const GantryMotorSpec& spec, double fallbackSec)
{
    if (!spec.configured) return fallbackSec;

    double vMax = deriveMaxGantryVelocityMmPerSec(spec);
    if (vMax <= 0.0) return fallbackSec; // invalid spec — don't block the user

    double aMax = spec.maxAccelMmPerSec2 > 0.0 ? spec.maxAccelMmPerSec2 : 1.0;
    double distanceMm = std::abs(toPt.gantryPositionMm - fromPt.gantryPositionMm);
    if (distanceMm < 1e-6) return 0.0; // no gantry move at all — no artificial floor

    math::TrapezoidalProfile profile(0.0, distanceMm, vMax, aMax);
    return profile.duration();
}

} // namespace motion
