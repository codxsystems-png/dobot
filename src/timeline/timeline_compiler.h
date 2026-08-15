#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Timeline Compiler
// Converts UI data models into a unified Timeline architecture for playback.
// ═══════════════════════════════════════════════════════════════════════════════

#include "timeline/timeline.h"
#include <memory>

class SegmentsModel;
class PointsModel;
class GantryService;
class FizService;
struct GantryMotorSpec;

namespace timeline {

class TimelineCompiler {
public:
    static std::shared_ptr<Timeline> compile(SegmentsModel* segments,
                                             PointsModel* points,
                                             GantryService* gantry,
                                             FizService* fiz,
                                             const GantryMotorSpec* motorSpec = nullptr);
};

} // namespace timeline
