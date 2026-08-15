#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Canonical Data Types
// FROZEN: Never change field names, types, or struct names between sessions.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QString>
#include <QDateTime>
#include <QImage>
#include <QList>
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
/// mirrored to GantryAxisController), since they're already integrated into
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
    double       countsPerUnit  = 100.0;  // encoder counts per mm (Linear) or per degree (Rotary)
    GantryLimits travelLimits;            // min/max travel, in axis units
    int          pwmRampPerTick = 15;     // max PWM change per 20ms control tick
    double       pidKp = 0.8;             // defaults mirror GantryAxisController's
    double       pidKi = 0.1;
    double       pidKd = 0.05;
    bool         configured = false;
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
    double        gantryPositionMm = 0.0; // Gantry position when point was recorded
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
};
