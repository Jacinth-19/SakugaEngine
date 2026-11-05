#pragma once
#include "post/PostSettings.h"
#include <array>
#include <cstdint>
namespace sk {
enum class PostTimingStage : uint32_t { Atmosphere, DepthOfField, Prefilter, Downsample, Upsample, Halation, Composite, Finish, Count };
constexpr uint32_t kPostTimingStages = static_cast<uint32_t>(PostTimingStage::Count);
constexpr uint32_t kFrameTimingQueries = 4 + 2 * kPostTimingStages;
using PostTimingActive = std::array<bool, kPostTimingStages>;
using PostTimingValues = std::array<float, kPostTimingStages>;
inline PostTimingActive postTimingPlan(bool atmosphere, bool dof, const PostPlan& plan, int bloomLevels) {
    return {atmosphere, dof, plan.bloom, plan.bloom && bloomLevels > 1, plan.bloom && bloomLevels > 1,
            plan.halation, true, plan.finish};
}
inline constexpr const char* kPostTimingNames[kPostTimingStages] = {
    "Atmosphere", "Depth of field", "Bloom prefilter", "Bloom downsample", "Bloom upsample", "Halation", "Composite", "Finishing"};
}
