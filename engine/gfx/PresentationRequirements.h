#pragma once
// GPU-free Vulkan presentation eligibility policy shared by platform probes.
// This is a necessary baseline, not a promise that every renderer format/limit is supported.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sk {
struct PresentationCandidate {
    std::string name;
    uint32_t apiMajor = 0, apiMinor = 0;
    int graphicsPresentQueue = -1;
    bool swapchain = false;
    bool surfaceFormats = false;
    bool transferDestination = false;
    bool colorAttachment = false;
    bool dynamicRendering = false;
    bool synchronization2 = false;
};
inline const char* presentationRequirementFailure(const PresentationCandidate& c) {
    if (c.apiMajor < 1 || (c.apiMajor == 1 && c.apiMinor < 3)) return "Vulkan 1.3 required";
    if (c.graphicsPresentQueue < 0) return "no graphics/present queue";
    if (!c.swapchain) return "VK_KHR_swapchain unavailable";
    if (!c.surfaceFormats) return "no surface formats";
    if (!c.transferDestination) return "surface lacks transfer destination";
    if (!c.colorAttachment) return "surface lacks color attachment";
    if (!c.dynamicRendering) return "dynamic rendering unavailable";
    if (!c.synchronization2) return "synchronization2 unavailable";
    return nullptr;
}
inline int firstPresentationCandidate(const std::vector<PresentationCandidate>& candidates) {
    for (std::size_t i = 0; i < candidates.size(); ++i) if (!presentationRequirementFailure(candidates[i])) return static_cast<int>(i);
    return -1;
}
}
