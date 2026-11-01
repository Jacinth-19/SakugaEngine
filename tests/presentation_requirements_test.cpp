#include "Check.h"
#include "gfx/PresentationRequirements.h"
#include <string>
#include <vector>
int main() {
    SK_TEST("presentation_requirements_test");
    sk::PresentationCandidate good; good.apiMajor = 1; good.apiMinor = 3; good.graphicsPresentQueue = 2;
    good.swapchain = good.surfaceFormats = good.transferDestination = good.colorAttachment = good.dynamicRendering = good.synchronization2 = true;
    CHECK(sk::presentationRequirementFailure(good) == nullptr);
    auto c = good; c.apiMinor = 2; CHECK(std::string(sk::presentationRequirementFailure(c)) == "Vulkan 1.3 required");
    c = good; c.graphicsPresentQueue = -1; CHECK(sk::presentationRequirementFailure(c) != nullptr);
    c = good; c.swapchain = false; CHECK(sk::presentationRequirementFailure(c) != nullptr);
    c = good; c.surfaceFormats = false; CHECK(sk::presentationRequirementFailure(c) != nullptr);
    c = good; c.transferDestination = false; CHECK(sk::presentationRequirementFailure(c) != nullptr);
    c = good; c.colorAttachment = false; CHECK(sk::presentationRequirementFailure(c) != nullptr);
    c = good; c.dynamicRendering = false; CHECK(sk::presentationRequirementFailure(c) != nullptr);
    c = good; c.synchronization2 = false; CHECK(sk::presentationRequirementFailure(c) != nullptr);
    CHECK(sk::firstPresentationCandidate({}) == -1);
    CHECK(sk::firstPresentationCandidate({c, good}) == 1);
    c = good; c.apiMajor = 2; c.apiMinor = 0; CHECK(sk::presentationRequirementFailure(c) == nullptr);
    return t_.finish();
}
