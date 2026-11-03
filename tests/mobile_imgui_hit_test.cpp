#include "Check.h"
#include <imgui.h>
#include "platform/MobileImGuiHitTest.h"

int main() {
    SK_TEST("mobile_imgui_hit_test");
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.DisplaySize = ImVec2(800, 600); io.DeltaTime = 1.f / 60.f;
    unsigned char* pixels; int w, h; io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    for (int i = 0; i < 1; ++i) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(70, 300));
        ImGui::SetNextWindowSize(ImVec2(250, 200));
        ImGui::Begin("Editor"); ImGui::TextUnformatted("Settings"); ImGui::End();
        ImGui::Render();
    }
    CHECK(sk::mobileHitsImGui(ImVec2(150, 450))); // also works immediately after the first frame
    CHECK(!sk::mobileHitsImGui(ImVec2(700, 500)));
    ImGui::NewFrame(); ImGui::Render();
    CHECK(!sk::mobileHitsImGui(ImVec2(150, 450))); // closed window does not steal gameplay touches
    ImGui::DestroyContext();
    return t_.finish();
}
