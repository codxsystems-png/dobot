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
/// For a StepDirClosedLoop axis the result is additionally capped by what the
/// board can actually clock: stepRateCeilingHz / stepsPerUnit. That cap
/// usually binds first, and ignoring it would advertise a speed the hardware
/// cannot produce — every segment time derived from it would then be a time
/// the axis silently fails to meet.
///
/// Returns 0.0 if gearRatio <= 0 (invalid spec) so callers can detect and
/// fall back safely instead of dividing by zero.
double deriveMaxGantryVelocityUnitsPerSec(const GantryMotorSpec& spec);

/// Steps per axis unit implied by the driver's pulses/rev, the gear ratio and
/// the travel per output rev. This is the stepper's equivalent of an encoder's
/// counts/unit, and it is what GantryTuning::countsPerUnit should be set to.
/// Returns 0.0 for an invalid spec.
double deriveStepsPerUnit(const GantryMotorSpec& spec);

/// Velocity the step-rate ceiling alone permits, ignoring motor RPM. 0.0 when
/// the axis is not a stepper or the spec is invalid.
double deriveStepCeilingVelocityUnitsPerSec(const GantryMotorSpec& spec);

/// True when the step-rate ceiling is what limits this axis, rather than motor
/// RPM. The setup dialog says which, because "why is my axis slow" has two very
/// different answers and the fix differs completely.
bool stepCeilingIsBinding(const GantryMotorSpec& spec);

/// UI label helpers, so axis units are written in exactly one place.
QString unitSuffix(const GantryMotorSpec& spec);     // " mm"    | "°"
QString unitLabel(const GantryMotorSpec& spec);      // "mm"     | "deg"
QString velocityLabel(const GantryMotorSpec& spec);  // "mm/s"   | "°/s"
QString accelLabel(const GantryMotorSpec& spec);     // "mm/s²"  | "°/s²"

/// Minimum physically-feasible duration (seconds) to move the axis by
/// `distanceUnits` (mm or degrees), given spec. If spec.configured is false,
/// or the spec is invalid (gearRatio <= 0), returns fallbackSec unchanged
/// rather than fabricating a physics answer from an unset/broken spec.
double minGantryDurationForDistanceSec(double distanceUnits,
                                        const GantryMotorSpec& spec, double fallbackSec);

/// As above, for the travel between two taught points' axis positions.
double minGantryDurationSec(const CameraPoint& fromPt, const CameraPoint& toPt,
                             const GantryMotorSpec& spec, double fallbackSec);

} // namespace motion
