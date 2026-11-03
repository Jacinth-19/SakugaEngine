#pragma once
// The render worker routes touches between frames. ImGui's FindHoveredWindowEx
// tests WasActive (the previous NewFrame), so it misses a newly presented window
// until another frame. Test the windows actually rendered in the last frame instead.
#include <imgui.h>
#include <imgui_internal.h>

namespace sk {
inline bool mobileHitsImGui(const ImVec2& pos) {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImVec2 regular = g.Style.TouchExtraPadding;
    const ImVec2 resize = ImMax(regular, ImVec2(g.Style.WindowBorderHoverPadding, g.Style.WindowBorderHoverPadding));
    for (int i = g.Windows.Size - 1; i >= 0; --i) {
        const ImGuiWindow* w = g.Windows[i];
        if (!w->Active || w->Hidden || (w->Flags & ImGuiWindowFlags_NoMouseInputs)) continue;
        const ImVec2 padding = (w->Flags & (ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) ? regular : resize;
        if (!w->OuterRectClipped.ContainsWithPad(pos, padding)) continue;
        if (w->HitTestHoleSize.x != 0) {
            ImVec2 origin(w->Pos.x + w->HitTestHoleOffset.x, w->Pos.y + w->HitTestHoleOffset.y);
            if (ImRect(origin, ImVec2(origin.x + w->HitTestHoleSize.x, origin.y + w->HitTestHoleSize.y)).Contains(pos)) continue;
        }
        return true;
    }
    return false;
}
}
