#ifndef ECS3D_GUICOMPONENTS_H
#define ECS3D_GUICOMPONENTS_H

#include <imgui.h>

namespace gc {
  inline void xyzGui(const char* label,
                     float* x,
                     float* y,
                     float* z,
                     const float sensitivity = 0.1f)
  {
    constexpr float DRAG_WIDTH = 80.0f;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    const float xyzWidth = 3 * (DRAG_WIDTH + ImGui::CalcTextSize("X ").x + ImGui::GetStyle().ItemSpacing.x);

    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - xyzWidth);

    ImGui::PushItemWidth(DRAG_WIDTH);

    ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "X");
    ImGui::SameLine();
    ImGui::DragFloat("##X", x, sensitivity);
    ImGui::SameLine();

    ImGui::TextColored(ImVec4(0.3f,1,0.3f,1), "Y");
    ImGui::SameLine();
    ImGui::DragFloat("##Y", y, sensitivity);
    ImGui::SameLine();

    ImGui::TextColored(ImVec4(0.3f,0.6f,1,1), "Z");
    ImGui::SameLine();
    ImGui::DragFloat("##Z", z, sensitivity);

    ImGui::PopItemWidth();
  }
}

#endif //ECS3D_GUICOMPONENTS_H