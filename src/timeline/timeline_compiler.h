#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Timeline Compiler
// Converts UI data models into a unified Timeline architecture for playback.
// ═══════════════════════════════════════════════════════════════════════════════

#include "timeline/timeline.h"
#include "core/types.h"
#include <memory>
#include <QList>

class SegmentsModel;
class PointsModel;
class GantryService;
class FizService;
struct GantryMotorSpec;
struct GantryTuning;

namespace timeline {

/// One external axis beyond the primary, with the keyframes taught for it.
///
/// The primary ("gantry") axis is still compiled from GantryService exactly as
/// before — leaving that path untouched is what keeps every existing project
/// compiling to the same timeline it always did. Additional axes are appended.
struct AxisTrackInput {
    AxisConfig            config;
    QList<GantryKeyframe> keyframes;
};

class TimelineCompiler {
public:
    static std::shared_ptr<Timeline> compile(SegmentsModel* segments,
                                             PointsModel* points,
                                             GantryService* gantry,
                                             FizService* fiz,
                                             const GantryMotorSpec* motorSpec = nullptr,
                                             const GantryTuning* tuning = nullptr,
                                             const QList<AxisTrackInput>* extraAxes = nullptr);
};

} // namespace timeline
