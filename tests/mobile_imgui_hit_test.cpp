#include "Check.h"
#include <imgui.h>
#include <imgui_internal.h>

int main() {
    SK_TEST("mobile_imgui_hit_test");
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.DisplaySize = ImVec2(800, 600); io.DeltaTime = 1.f / 60.f;
    unsigned char* pixels; int w, h; io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    for (int i = 0; i < 2; ++i) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(70, 300));
        ImGui::SetNextWindowSize(ImVec2(250, 200));
        ImGui::Begin("Editor"); ImGui::TextUnformatted("Settings"); ImGui::End();
        ImGui::Render();
    }
    ImGuiWindow* hovered = nullptr;
    ImGui::FindHoveredWindowEx(ImVec2(150, 450), true, &hovered, nullptr);
    CHECK(hovered != nullptr); // an editor below the old fixed upper-screen hit region
    ImGui::FindHoveredWindowEx(ImVec2(700, 500), true, &hovered, nullptr);
    CHECK(hovered == nullptr);
    ImGui::DestroyContext();
    return t_.finish();
}
