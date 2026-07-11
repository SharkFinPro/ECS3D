#ifndef ECS3D_EDITORTHEME_H
#define ECS3D_EDITORTHEME_H

#include <imgui.h>

// Design tokens + global style for the ECS3D editor (see "ECS3D Editor.dc" mockup).
// Both the global ImGuiStyle (setupImGuiStyle) and the custom widgets in GuiComponents.h pull their
// colors from here so they stay consistent. Fonts and the custom title bar are intentionally out of scope.
namespace theme {
  inline ImVec4 v4(const int r, const int g, const int b, const int a = 255)
  {
    return { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
  }

  // --- palette (mirrors the CSS custom properties in the mockup) ---
  inline const ImVec4 bg      = v4(20, 21, 24);    // --bg
  inline const ImVec4 panel   = v4(28, 29, 34);    // --panel
  inline const ImVec4 head    = v4(38, 41, 50);    // --head
  inline const ImVec4 inset   = v4(15, 16, 19);    // --inset
  inline const ImVec4 hover   = v4(35, 38, 46);    // --hover
  inline const ImVec4 line    = v4(42, 45, 53);    // --line
  inline const ImVec4 line2   = v4(54, 58, 68);    // --line2
  inline const ImVec4 t1      = v4(231, 232, 236); // --t1 primary text
  inline const ImVec4 t2      = v4(148, 152, 163); // --t2 secondary text
  inline const ImVec4 t3      = v4(100, 106, 118); // --t3 muted text

  // Accent (default cyan #1fb8d4). accdim is the translucent accent used for fills/glows.
  inline const ImVec4 accent  = v4(31, 184, 212);
  inline const ImVec4 accdim  = v4(31, 184, 212, 38);
  inline const ImVec4 accSoft = v4(31, 184, 212, 60);
  inline const ImVec4 onAcc   = v4(6, 34, 42);     // text/icon color on solid accent (#06222a)

  // Axis colors.
  inline const ImVec4 axisX   = v4(229, 86, 91);
  inline const ImVec4 axisY   = v4(92, 191, 106);
  inline const ImVec4 axisZ   = v4(77, 147, 245);

  // Danger (delete hover).
  inline const ImVec4 danger  = v4(229, 86, 91);

  // Asset-type accent colors (used by the asset grid badges + the model renderer slots).
  inline const ImVec4 modelPurple = v4(167, 139, 250); // #a78bfa
  inline const ImVec4 scriptAmber = v4(230, 179, 90);  // #e6b35a
  inline const ImVec4 sceneGreen  = v4(92, 194, 133);  // #5cc285
  inline const ImVec4 prefabBlue  = v4(96, 165, 250);  // #60a5fa

  inline ImU32 u32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

  // Applies the design tokens to the global ImGuiStyle. Call after the ImGui context is current.
  inline void applyStyle()
  {
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowPadding     = ImVec2(14.0f, 14.0f);
    s.FramePadding      = ImVec2(11.0f, 7.0f);
    s.ItemSpacing       = ImVec2(10.0f, 9.0f);
    s.ItemInnerSpacing  = ImVec2(8.0f, 6.0f);
    s.CellPadding       = ImVec2(8.0f, 6.0f);
    s.IndentSpacing     = 18.0f;
    s.ScrollbarSize     = 11.0f;
    s.GrabMinSize       = 12.0f;

    // The mockup leans on consistent ~6-7px rounding everywhere.
    s.WindowRounding    = 0.0f;   // docked panels stay square at their edges
    s.ChildRounding     = 7.0f;
    s.FrameRounding     = 6.0f;
    s.PopupRounding     = 8.0f;
    s.ScrollbarRounding = 7.0f;
    s.GrabRounding      = 6.0f;
    s.TabRounding       = 6.0f;

    s.WindowBorderSize  = 1.0f;
    s.ChildBorderSize   = 1.0f;
    s.FrameBorderSize   = 1.0f;
    s.PopupBorderSize   = 1.0f;
    s.TabBorderSize     = 0.0f;

    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.WindowTitleAlign  = ImVec2(0.0f, 0.5f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]            = t1;
    c[ImGuiCol_TextDisabled]    = t3;
    c[ImGuiCol_WindowBg]        = panel;
    c[ImGuiCol_ChildBg]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]         = head;
    c[ImGuiCol_Border]          = line;
    c[ImGuiCol_BorderShadow]    = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]         = inset;
    c[ImGuiCol_FrameBgHovered]  = inset;        // hover is signalled via border in the custom widgets
    c[ImGuiCol_FrameBgActive]   = inset;

    c[ImGuiCol_TitleBg]         = v4(23, 24, 28);
    c[ImGuiCol_TitleBgActive]   = v4(23, 24, 28);
    c[ImGuiCol_TitleBgCollapsed]= v4(23, 24, 28);
    c[ImGuiCol_MenuBarBg]       = v4(23, 24, 28);

    c[ImGuiCol_ScrollbarBg]     = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]   = line2;
    c[ImGuiCol_ScrollbarGrabHovered] = v4(71, 75, 85);
    c[ImGuiCol_ScrollbarGrabActive]  = v4(71, 75, 85);

    c[ImGuiCol_CheckMark]       = accent;
    c[ImGuiCol_SliderGrab]      = accent;
    c[ImGuiCol_SliderGrabActive]= accent;

    // Headers double as the component section bars (--head with accent hover).
    c[ImGuiCol_Header]          = head;
    c[ImGuiCol_HeaderHovered]   = v4(44, 48, 58);
    c[ImGuiCol_HeaderActive]    = head;

    c[ImGuiCol_Button]          = head;
    c[ImGuiCol_ButtonHovered]   = hover;
    c[ImGuiCol_ButtonActive]    = line2;

    c[ImGuiCol_Separator]       = line;
    c[ImGuiCol_SeparatorHovered]= line2;
    c[ImGuiCol_SeparatorActive] = line2;

    c[ImGuiCol_ResizeGrip]        = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = accSoft;
    c[ImGuiCol_ResizeGripActive]  = accent;

    c[ImGuiCol_Tab]               = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered]        = hover;
    c[ImGuiCol_TabActive]         = panel;
    c[ImGuiCol_TabUnfocused]      = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabUnfocusedActive]= panel;

    c[ImGuiCol_TextSelectedBg]  = accSoft;
    c[ImGuiCol_DragDropTarget]  = accent;
    c[ImGuiCol_NavHighlight]    = accent;
    c[ImGuiCol_ModalWindowDimBg]= v4(10, 11, 13, 150);
  }
}

#endif //ECS3D_EDITORTHEME_H
