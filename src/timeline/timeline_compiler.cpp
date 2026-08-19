#include "timeline/timeline_compiler.h"
#include "timeline/track_impls.h"
#include "models/segments_model.h"
#include "models/points_model.h"
#include "application/gantryservice.h"
#include "application/fizservice.h"
#include "hardware/dobot_adapter.h"
#include "core/motion_estimator.h"

namespace timeline {

std::shared_ptr<Timeline> TimelineCompiler::compile(SegmentsModel* segments,
                                                    PointsModel* points,
                                                    GantryService* gantry,
                                                    FizService* fiz,
                                                    const GantryMotorSpec* motorSpec,
                                                    const GantryTuning* tuning,
                                                    const QList<AxisTrackInput>* extraAxes)
{
    auto timeline = std::make_shared<Timeline>();

    // Compile Tracks directly from SegmentsModel
    if (segments && points) {
        auto robotTrack = std::make_shared<RobotTrack>("robot");
        auto gantryTrack = std::make_shared<GantryTrack>("gantry");
        auto fizTrack = std::make_shared<FizTrack>("fiz");

        // Only override GantryTrack's built-in defaults when the user has
        // actually configured a real motor spec — otherwise leave it alone
        // so unconfigured projects (old or new) behave exactly as before.
        if (motorSpec && motorSpec->configured) {
            double vMax = motion::deriveMaxGantryVelocityUnitsPerSec(*motorSpec);
            if (vMax > 0.0) {
                gantryTrack->setLimits(vMax, motorSpec->maxAccelMmPerSec2);
            }
        }

        // Travel range, so an out-of-reach keyframe is rejected by preflight
        // rather than only being clamped at runtime by the controller.
        if (tuning && tuning->configured) {
            gantryTrack->setRangeLimits(tuning->travelLimits);
        }

        for (int i = 0; i < segments->rowCount(); ++i) {
            TimelineSegment seg = segments->segmentAt(i);
            if (!seg.enabled) continue; // muted — skip entirely, not just "don't fire"
            CameraPoint pt = points->pointById(seg.pointId);
            if (!pt.id.isEmpty()) {
                // Robot Keyframe
                hardware::DobotMoveTarget target;
                target.moveType = seg.type;
                target.targetPose = pt.pose;
                target.speedPct = seg.speedPct;
                target.accPct = seg.accPct;
                target.cpValue = seg.cpValue;
                target.pauseAfter = seg.pauseAfter;
                
                if (seg.type == TimelineSegment::Arc) {
                    CameraPoint viaPt = points->pointById(seg.arcViaPointId);
                    if (!viaPt.id.isEmpty()) {
                        target.viaPose = viaPt.pose;
                    } else {
                        target.moveType = TimelineSegment::MovJ; // Fallback
                    }
                }

                TrackKeyframe rKf;
                rKf.id = seg.id + "_robot";
                rKf.time = seg.triggerTime;
                rKf.value = QVariant::fromValue(target);
                robotTrack->addKeyframe(rKf);

                // Gantry Keyframe
                if (gantry) {
                    TrackKeyframe gKf;
                    gKf.id = seg.id + "_gantry";
                    gKf.time = seg.triggerTime;
                    gKf.value = QVariant::fromValue(pt.gantryPositionMm);
                    gantryTrack->addKeyframe(gKf);
                }

                // FIZ Keyframe
                if (fiz) {
                    TrackKeyframe fKf;
                    fKf.id = seg.id + "_fiz";
                    fKf.time = seg.triggerTime;
                    fKf.value = QVariant::fromValue(pt.fizState);
                    fizTrack->addKeyframe(fKf);
                }
            }
        }
        
        timeline->addTrack(robotTrack);

        // Merge standalone gantry keyframes from GantryService
        // (these are added via the track widget or "Add to Timeline" button,
        //  independent of the per-segment CameraPoint gantry positions above)
        if (gantry) {
            for (const auto& gkf : gantry->keyframes()) {
                TrackKeyframe tkf;
                tkf.id    = gkf.id + "_gantry";
                tkf.time  = gkf.time;
                tkf.value = QVariant::fromValue(gkf.positionMm);
                gantryTrack->addKeyframe(tkf);
            }
            timeline->addTrack(gantryTrack);
        }

        // ─── Additional external axes ────────────────────────────────────
        // One track each, keyed by the axis's own id. Each gets limits derived
        // from ITS OWN motor spec and travel range, so preflight rejects a
        // keyframe that axis cannot reach rather than checking every axis
        // against the primary's numbers.
        if (extraAxes) {
            for (const AxisTrackInput& axis : *extraAxes) {
                if (axis.config.id.isEmpty() || axis.config.id == "gantry") continue;

                auto track = std::make_shared<GantryTrack>(axis.config.id);

                if (axis.config.motorSpec.configured) {
                    const double vMax =
                        motion::deriveMaxGantryVelocityUnitsPerSec(axis.config.motorSpec);
                    if (vMax > 0.0) {
                        track->setLimits(vMax, axis.config.motorSpec.maxAccelMmPerSec2);
                    }
                }
                if (axis.config.tuning.configured) {
                    track->setRangeLimits(axis.config.tuning.travelLimits);
                }

                for (const auto& gkf : axis.keyframes) {
                    TrackKeyframe tkf;
                    tkf.id    = gkf.id + "_" + axis.config.id;
                    tkf.time  = gkf.time;
                    tkf.value = QVariant::fromValue(gkf.positionMm);
                    track->addKeyframe(tkf);
                }
                timeline->addTrack(track);
            }
        }

        // Merge standalone FIZ keyframes from FizService
        if (fiz) {
            for (const auto& fkf : fiz->keyframes()) {
                TrackKeyframe tkf;
                tkf.id    = fkf.id + "_fiz";
                tkf.time  = fkf.time;
                tkf.value = QVariant::fromValue(fkf.state);
                fizTrack->addKeyframe(tkf);
            }
            timeline->addTrack(fizTrack);
        }
    }

    return timeline;
}

} // namespace timeline
