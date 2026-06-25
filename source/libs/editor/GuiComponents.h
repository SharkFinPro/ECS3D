#ifndef ECS3D_GUICOMPONENTS_H
#define ECS3D_GUICOMPONENTS_H

#include <imgui.h>

// Small shared ImGui widgets for the component editors (migrated from core/GuiComponents.h). Lives in
// ECS3DEditorLib next to the handlers that use it.
namespace gc {
  // A labelled X/Y/Z drag row. Returns true if any of the three was edited this frame.
  inline bool xyzGui(const char* label,
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

    bool edited = false;

    ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "X");
    ImGui::SameLine();
    edited |= ImGui::DragFloat("##X", x, sensitivity);
    ImGui::SameLine();

    ImGui::TextColored(ImVec4(0.3f,1,0.3f,1), "Y");
    ImGui::SameLine();
    edited |= ImGui::DragFloat("##Y", y, sensitivity);
    ImGui::SameLine();

    ImGui::TextColored(ImVec4(0.3f,0.6f,1,1), "Z");
    ImGui::SameLine();
    edited |= ImGui::DragFloat("##Z", z, sensitivity);

    ImGui::PopItemWidth();

    return edited;
  }
}

#endif //ECS3D_GUICOMPONENTS_H
