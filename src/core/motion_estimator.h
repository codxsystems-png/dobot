#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Motion Estimator
// Derives real gantry feasibility (max velocity from motor spec, minimum move
// duration) so segment trigger times can be auto-computed instead of guessed.
// ═══════════════════════════════════════════════════════════════════════════════

#include "core/types.h"
#include <QString>

namespace motion {

/// Derives max velocity in axis units per second from a GantryMotorSpec:
///   outputRpm = motorRpm / gearRatio
///   Linear: maxVelocity = outputRpm * mmPerRev / 60.0   (mm/s)
///   Rotary: maxVelocity = outputRpm * 360.0   / 60.0    (deg/s — one output
///           revolution is 360 degrees by definition, so mmPerRev is unused)
/// Returns 0.0 if gearRatio <= 0 (invalid spec) so callers can detect and
/// fall back safely instead of dividing by zero.
double deriveMaxGantryVelocityUnitsPerSec(const GantryMotorSpec& spec);

/// UI label helpers, so axis units are written in exactly one place.
QString unitSuffix(const GantryMotorSpec& spec);     // " mm"    | "°"
QString unitLabel(const GantryMotorSpec& spec);      // "mm"     | "deg"
QString velocityLabel(const GantryMotorSpec& spec);  // "mm/s"   | "°/s"
QString accelLabel(const GantryMotorSpec& spec);     // "mm/s²"  | "°/s²"

/// Minimum physically-feasible duration (seconds) for the gantry to move
/// from fromPt.gantryPositionMm to toPt.gantryPositionMm, given spec. If
/// spec.configured is false, or the spec is invalid (gearRatio <= 0), returns
/// fallbackSec unchanged rather than fabricating a physics answer from an
/// unset/broken spec.
double minGantryDurationSec(const CameraPoint& fromPt, const CameraPoint& toPt,
                             const GantryMotorSpec& spec, double fallbackSec);

} // namespace motion
