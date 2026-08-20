#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Canonical Data Types
// FROZEN: Never change field names, types, or struct names between sessions.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QString>
#include <QDateTime>
#include <QImage>
#include <QList>
#include <QMap>
#include <cstdint>

// ─── Robot Joint Angles ────────────────────────────────────────────────────────
struct JointAngles {
    double j[6] = {};  // J1–J6 in degrees
};

// ─── Cartesian Pose ────────────────────────────────────────────────────────────
struct CartesianPose {
    double x  = 0.0, y  = 0.0, z  = 0.0;    // millimeters
    double rx = 0.0, ry = 0.0, rz = 0.0;     // degrees
};

// ─── Robot Mode (from Dobot TCP-IP protocol) ───────────────────────────────────
enum class RobotMode : int {
    Init       = 1,
    BrakeOpen  = 2,
    PowerOff   = 3,
    Disabled   = 4,
    Idle       = 5,
    Drag       = 6,
    Running    = 7,
    SingleMove = 8,
    Error      = 9,
    Pause      = 10,
    Collision  = 11
};

// ─── FIZ (Focus / Iris / Zoom) — Tilta Nucleus-M ──────────────────────────────

/// FIZ motor state — all values 0.0–100.0 percent
struct FizState {
    float focus = 0.0f;   // 0% = close focus, 100% = infinity
    float iris  = 0.0f;   // 0% = wide open,   100% = closed
    float zoom  = 0.0f;   // 0% = wide angle,  100% = telephoto
};

/// FIZ keyframe on the timeline
struct FizKeyframe {
    QString  id;            // QUuid::createUuid().toString()
    double   time = 0.0;   // seconds from timeline start
    FizState state;         // focus/iris/zoom values at this time

    enum class Easing {
        Linear,
        EaseIn,
        EaseOut,
        EaseInOut
    } easing = Easing::Linear;
};

// ─── Gantry (Linear Axis) ─────────────────────────────────────────────────────

struct GantryKeyframe {
    QString  id;            // QUuid::createUuid().toString()
    double   time = 0.0;   // seconds from timeline start
    double   positionMm = 0.0; // gantry position in mm

    enum class Easing {
        Linear,
        EaseIn,
        EaseOut,
        EaseInOut
    } easing = Easing::Linear;
};

/// Optional real-world lens mapping (UI display only)
struct LensMapping {
    float   focusNearMm = 300.0f;    // closest focus distance in mm
    float   focusFarMm  = 5000.0f;   // furthest focus distance in mm
    float   zoomWideMm  = 24.0f;     // wide end focal length in mm
    float   zoomTeleMm  = 85.0f;     // tele end focal length in mm
    QString lensName    = "";        // e.g. "Canon 24-85mm f/3.5"
};

// ─── Safety Limits (Phase 2: real bounds/feasibility checking) ────────────────

/// Physical travel range for the linear gantry axis. Velocity/acceleration
/// limits are configured separately, directly on timeline::GantryTrack (and
/// mirrored to the axis controller), since they're already integrated into
/// its trapezoidal motion profiling — this struct only covers position range.
struct GantryLimits {
    double minMm = 0.0;
    double maxMm = 1000.0;
};

/// What the external axis physically is. Linear drives a gantry/slider and
/// works in millimetres; Rotary is a bare motor with no linear conversion and
/// works in degrees. The same Arduino firmware serves both — only the
/// host-side unit derivation and the UI labels differ.
enum class GantryAxisType : int {
    Linear = 0,   // gantry / slider — units are mm
    Rotary = 1    // bare motor — units are degrees
};

/// Real motor-spec parameters for the external axis, used to DERIVE its
/// max velocity (see motion::deriveMaxGantryVelocityUnitsPerSec). Max
/// acceleration can't be derived from RPM/gear ratio alone (needs torque/
/// load data), so it stays a direct-entry field. `configured` is the real
/// gate: false means "user hasn't set this up yet" — every consumer must
/// skip using this spec entirely rather than fabricate a value, so
/// unconfigured projects (old or new) behave exactly like the pre-existing
/// hardcoded GantryTrack defaults.
struct GantryMotorSpec {
    double motorRpm          = 3000.0;
    double gearRatio         = 1.0;   // motor revs per ONE output/leadscrew rev (10:1 reducer => 10)
    double mmPerRev          = 4.0;   // travel per output rev: leadscrew pitch or pi*pulley dia (Linear only)
    double maxAccelMmPerSec2 = 400.0; // not derivable from RPM/gear ratio — direct entry
    bool   configured        = false;
    GantryAxisType axisType  = GantryAxisType::Linear;

    /// The driver's pulses/rev DIP setting. Combined with
    /// gearRatio and mmPerRev this gives steps per unit, which is the same
    /// physical constant GantryTuning::countsPerUnit holds for a DC encoder.
    double pulsesPerRev      = 1600.0;

    /// Max step rate the axis can actually SUSTAIN.
    ///
    /// Lives here rather than with the tuning because it is an INPUT TO
    /// VELOCITY DERIVATION, and it usually binds before motor RPM does — at
    /// 1600 p/r on a 4mm screw, this is 8.75mm/s while the motor's own rating
    /// would suggest 200. Deriving from RPM alone advertises a speed the axis
    /// cannot produce, and every segment time computed from it would be a lie.
    ///
    /// The default is deliberately conservative. This is a MEASURED quantity —
    /// bench-sweep the axis and watch the shaft, because the board's own step
    /// count cannot see a lost step and will report every rate as fine. On the
    /// first rig measured, a free shaft followed cleanly to 5000 and dropped
    /// ~800 steps in 64000 at 8000; the checklist takes 70% of the last clean
    /// rate. A coupled load lowers it further, so re-measure after coupling.
    double stepRateCeilingHz = 3500.0;
};

/// Runtime closed-loop configuration for the external axis. Deliberately
/// SEPARATE from GantryMotorSpec: that struct's `configured` flag gates
/// trajectory-feasibility math and is copied by value into SegmentsModel,
/// TimelineScene and PropertiesPanel, none of which want PID gains. Two
/// independent `configured` flags means tuning the PID can't silently switch
/// on physics-based trigger-time flooring against an unentered motor spec.
///
/// `travelLimits` reuses GantryLimits; its minMm/maxMm members are read as
/// generic axis units (mm when Linear, degrees when Rotary).
struct GantryTuning {
    /// Steps per unit of axis travel: mm for a Linear axis, degrees for a
    /// Rotary one. Derivable from the driver's pulses/rev, the gear ratio and
    /// the travel per output rev — see motion::deriveStepsPerUnit.
    double       countsPerUnit  = 100.0;
    GantryLimits travelLimits;            // min/max travel, in axis units
    bool         configured = false;

    // The step-rate ceiling lives in GantryMotorSpec, not here — see the note
    // there. These two are board-side clamps and policy, not motion inputs.
    /// Step acceleration clamp, applied on the board.
    ///
    /// Deliberately gentle. 40000 was inherited from the DC era and reaches
    /// full speed in well under a tenth of a second — on a loaded axis that
    /// is a slam, and a closed-loop drive answers it by losing position and
    /// raising its alarm, which reads as "the axis randomly stops".
    ///
    /// Raise it only as far as the axis actually follows, checking the SHAFT
    /// returns to its mark rather than trusting the step count.
    double stepAccelStepsPerSec2  = 8000.0;
    /// Whether to drop ENABLE when idle. Stays false on anything gravity-
    /// loaded: ENABLE is a torque switch, and releasing it lets the axis fall.
    bool   idleDisable            = false;
};

/// Per-channel bounds for FIZ (focus/iris/zoom) setpoints, in percent.
struct FizLimits {
    float minPercent = 0.0f;
    float maxPercent = 100.0f;
};

/// Cartesian workspace envelope for the robot arm. Joint-space limits aren't
/// checked here because timeline keyframes only carry a Cartesian target
/// (see hardware::DobotMoveTarget) — no inverse-kinematics/joint data exists
/// on this path yet.
struct WorkspaceLimits {
    double minX = -1000.0, maxX = 1000.0;
    double minY = -1000.0, maxY = 1000.0;
    double minZ =     0.0, maxZ = 1000.0;
};

// ─── Camera Point (taught position) ───────────────────────────────────────────
struct CameraPoint {
    QString       id;          // QUuid::createUuid().toString()
    QString       name;
    JointAngles   joints;
    CartesianPose pose;
    QImage        thumbnail;   // 160×90 JPEG
    QDateTime     recorded;
    FizState      fizState;    // FIZ motor positions when point was recorded
    // FROZEN field name. Stays the store for the PRIMARY axis, because
    // motion_estimator, timeline_compiler and the teach flow all read it
    // directly and a rename would touch every one of them for no gain.
    double        gantryPositionMm = 0.0; // Gantry position when point was recorded

    /// Position of every axis when the point was taught, keyed by axis id.
    ///
    /// The primary axis appears BOTH here (as "gantry") and in
    /// gantryPositionMm above, kept in step by axisPosition()/setAxisPosition().
    /// Same reasoning as Project::axes: consumers migrate one at a time, and
    /// an older build still finds the flat field it expects.
    QMap<QString, double> axisPositions;

    /// Position of `axisId`, falling back to the frozen field for the primary
    /// axis so a point taught before multi-axis still answers correctly.
    double axisPosition(const QString& axisId) const {
        if (axisPositions.contains(axisId)) return axisPositions.value(axisId);
        return (axisId == "gantry") ? gantryPositionMm : 0.0;
    }

    /// Writes both representations for the primary axis, so nothing that
    /// reads only the frozen field goes stale.
    void setAxisPosition(const QString& axisId, double units) {
        axisPositions.insert(axisId, units);
        if (axisId == "gantry") gantryPositionMm = units;
    }
};

// ─── Timeline Segment ─────────────────────────────────────────────────────────
struct TimelineSegment {
    QString id;
    QString pointId;
    double  triggerTime = 0.0;      // seconds from timeline start

    enum Type { MovJ, MovL, Arc } type = MovJ;

    int     speedPct    = 80;       // 1-100
    int     accPct      = 50;       // 1-100
    double  cpValue     = 0.0;      // 0 = STOP, >0 = blend ratio
    double  blendRadius = 0.0;      // mm, 0 = use cpValue instead
    double  preWait     = 0.0;      // seconds before move
    double  postWait    = 0.0;      // seconds after arrival
    QString arcViaPointId;          // empty unless type == Arc

    enum CamTrigger { None, StartRecord, StopRecord, TakePhoto } camTrigger = None;
    enum TriggerAt  { AtStart, AtEnd, AtBoth } triggerAt = AtStart;

    // Disabled segments are skipped entirely by TimelineCompiler (mute a
    // step without deleting it). pauseAfter halts playback once this
    // segment's move is confirmed complete, until the operator resumes —
    // for a manual camera reload, a set change, etc.
    bool enabled    = true;
    bool pauseAfter = false;
};

// ─── Project ──────────────────────────────────────────────────────────────────
/// One external axis, everything needed to configure and drive it.
///
/// driveKind and axisType are deliberately NOT repeated here — they live in
/// motorSpec, and duplicating them would create two answers to "what kind of
/// axis is this" that could disagree after a partial edit.
/// The most axes one board drives. Three step/dir channels fit an Uno
/// comfortably now that there is no encoder decoder competing for interrupts;
/// beyond that the pin table, not the CPU, is the limit.
constexpr int kMaxAxes = 3;

struct AxisConfig {
    /// Stable key, used in the timeline's track map and in saved files. The
    /// first axis is "gantry" forever: it is what every pre-existing project
    /// and every not-yet-migrated consumer refers to.
    QString id = "gantry";
    QString displayName = "Gantry";

    GantryMotorSpec motorSpec;
    GantryTuning    tuning;

    /// Serial port shared with any other axes on the same board.
    QString portName;

    /// Board address of this axis. Several axes share one board and are told
    /// apart only by this, so two axes must never carry the same index — the
    /// link routes replies by it, and a duplicate silently steals them.
    int firmwareAxisIndex = 0;
};

struct Project {
    QString                   name;
    QString                   version = "1.2.0";
    QDateTime                 created;
    QList<CameraPoint>        points;
    QList<TimelineSegment>    segments;
    double                    timelineDuration = 30.0; // seconds

    // Camera
    QString                   cameraSourceType = "usb"; // "usb" | "ip"
    QString                   cameraSourceUrl;
    QString                   cameraResolution = "1920x1080";
    int                       cameraFramerate  = 30;

    // FIZ (Tilta Nucleus-M lens control)
    QList<FizKeyframe>        fizKeyframes;
    LensMapping               lensMapping;

    // Gantry
    QList<GantryKeyframe>     gantryKeyframes;
    // LEGACY: kept for .crp back-compat and mirrored on save, but never read
    // at runtime — gantryTuning.countsPerUnit is the source of truth.
    double                    gantryEncoderCountsPerMm = 100.0;
    GantryMotorSpec           gantryMotorSpec;
    GantryTuning              gantryTuning;

    // ─── Multi-axis (schema 2) ────────────────────────────────────────────
    // The singular gantry* members above remain the source of truth for the
    // FIRST axis and are mirrored to and from axes[0] on every load and save.
    // That is what lets consumers migrate one at a time instead of in a
    // single sweep, and what lets an older build still open a new file.
    QList<AxisConfig> axes;
    /// Keyframes per axis id. gantryKeyframes above stays the store for the
    /// "gantry" axis; this holds any additional ones.
    QMap<QString, QList<GantryKeyframe>> axisKeyframes;
};
