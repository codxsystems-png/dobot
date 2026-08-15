#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Motion Estimator
// Derives real gantry feasibility (max velocity from motor spec, minimum move
// duration) so segment trigger times can be auto-computed instead of guessed.
// ═══════════════════════════════════════════════════════════════════════════════

#include "core/types.h"

namespace motion {

/// Derives max linear velocity (mm/s) from a GantryMotorSpec:
///   outputRpm = motorRpm / gearRatio
///   maxVelocityMmPerSec = outputRpm * mmPerRev / 60.0
/// Returns 0.0 if gearRatio <= 0 (invalid spec) so callers can detect and
/// fall back safely instead of dividing by zero.
double deriveMaxGantryVelocityMmPerSec(const GantryMotorSpec& spec);

/// Minimum physically-feasible duration (seconds) for the gantry to move
/// from fromPt.gantryPositionMm to toPt.gantryPositionMm, given spec. If
/// spec.configured is false, or the spec is invalid (gearRatio <= 0), returns
/// fallbackSec unchanged rather than fabricating a physics answer from an
/// unset/broken spec.
double minGantryDurationSec(const CameraPoint& fromPt, const CameraPoint& toPt,
                             const GantryMotorSpec& spec, double fallbackSec);

} // namespace motion
