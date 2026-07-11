#ifndef ECS3D_GUICOMPONENTS_H
#define ECS3D_GUICOMPONENTS_H

#include "EditorTheme.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

// Small shared ImGui widgets for the component editors, living in ECS3DEditorLib next to the handlers
// that use it.
//
// The widgets below the original xyzGui render with ImDrawList to match the "ECS3D Editor.dc" mockup
// (boxed axis fields, accent track sliders, pill badges, filled accent checkboxes). They are drop-in
// replacements for the stock ImGui calls in the component editors.
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

  // ------------------------------------------------------------------------------------------------
  // Custom-drawn (ImDrawList) widgets
  // ------------------------------------------------------------------------------------------------

  // Minimum width of the secondary-text label column on the left of inspector rows.
  constexpr float kLabelColumn = 72.0f;

  // Draws a muted row label and advances to the control column (at least kLabelColumn, but always past
  // the label text so the control never overlaps it).
  // Gap kept between the end of a row label and the start of its control.
  constexpr float kLabelGap = 22.0f;

  inline void rowLabel(const char* label)
  {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::t2, "%s", label);
    ImGui::SameLine(std::max(kLabelColumn, ImGui::CalcTextSize(label).x + kLabelGap));
  }

  // Draws a rounded "pill" badge (e.g. the object count or the selected-object type chip) at the
  // current cursor and advances the cursor past it. Pure decoration, no interaction.
  inline void pill(const char* text, const ImVec4& textCol = theme::t2, const ImVec4& bgCol = theme::inset)
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 pad(9.0f, 3.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(textSize.x + pad.x * 2.0f, textSize.y + pad.y * 2.0f);

    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), theme::u32(bgCol), size.y * 0.5f);
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), theme::u32(textCol), text);

    ImGui::Dummy(size);
  }

  // Draws `text` to `dl` at `pos` with manual per-glyph letter spacing (ImGui has no tracking control),
  // optionally uppercasing each glyph. Returns the drawn width. Does not touch the ImGui cursor.
  inline float drawSpacedText(ImDrawList* dl, const ImVec2& pos, const ImU32 col, const char* text,
                              const float letterSpacing = 1.5f, const bool upper = false)
  {
    float x = pos.x;
    for (const char* c = text; *c; ++c)
    {
      const char glyph[2] = {
        upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(*c))) : *c, '\0'
      };
      dl->AddText(ImVec2(x, pos.y), col, glyph);
      x += ImGui::CalcTextSize(glyph).x + letterSpacing;
    }
    return x - pos.x - letterSpacing;
  }

  // A small-caps panel/section label (uppercased, letter-spaced, muted) matching the mockup's
  // "OBJECTS" / "SELECTED OBJECT" / "SCRIPTS" headers. Advances the ImGui cursor past the label.
  // Returns its drawn width.
  inline float sectionLabel(const char* text, const float letterSpacing = 1.5f,
                            const ImVec4& col = theme::t2)
  {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = drawSpacedText(ImGui::GetWindowDrawList(), pos, theme::u32(col), text,
                                       letterSpacing, true);
    ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
    return width;
  }

  // A filled-accent checkbox matching the mockup (rounded inset box, accent fill + check when on).
  // Behaves like ImGui::Checkbox: returns true on the frame the value changes.
  inline bool accentCheckbox(const char* label, bool* v)
  {
    const float box = 18.0f;
    const float h = std::max(box, ImGui::GetFrameHeight());
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(label);
    const bool pressed = ImGui::InvisibleButton("##cb", ImVec2(ImGui::GetContentRegionAvail().x, h));
    ImGui::PopID();
    if (pressed)
    {
      *v = !*v;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 boxMin(pos.x, pos.y + (h - box) * 0.5f);
    const ImVec2 boxMax(boxMin.x + box, boxMin.y + box);

    dl->AddRectFilled(boxMin, boxMax, theme::u32(*v ? theme::accent : theme::inset), 5.0f);
    if (!*v)
    {
      dl->AddRect(boxMin, boxMax, theme::u32(theme::line2), 5.0f);
    }
    else
    {
      // Check mark.
      const ImU32 mark = theme::u32(theme::onAcc);
      const ImVec2 c0(boxMin.x + box * 0.26f, boxMin.y + box * 0.52f);
      const ImVec2 c1(boxMin.x + box * 0.44f, boxMin.y + box * 0.70f);
      const ImVec2 c2(boxMin.x + box * 0.76f, boxMin.y + box * 0.32f);
      dl->AddLine(c0, c1, mark, 2.2f);
      dl->AddLine(c1, c2, mark, 2.2f);
    }

    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(boxMax.x + 11.0f, pos.y + (h - ts.y) * 0.5f), theme::u32(theme::t1), label);

    return pressed;
  }

  // A single boxed axis field: rounded inset box with a colored axis letter and a borderless DragFloat.
  inline bool axisField(const char* axis, const ImVec4& axisCol, float* v, float width, float sensitivity)
  {
    const float h = ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const bool hovered = ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + width, pos.y + h));
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h), theme::u32(theme::inset), 5.0f);
    dl->AddRect(pos, ImVec2(pos.x + width, pos.y + h), theme::u32(hovered ? theme::line2 : theme::line), 5.0f);

    const float padX = 8.0f;
    const ImVec2 axisSize = ImGui::CalcTextSize(axis);
    dl->AddText(ImVec2(pos.x + padX, pos.y + (h - axisSize.y) * 0.5f), theme::u32(axisCol), axis);

    // Transparent DragFloat over the remaining width.
    ImGui::PushID(axis);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + padX + axisSize.x, pos.y));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::SetNextItemWidth(width - padX * 2.0f - axisSize.x);
    const bool edited = ImGui::DragFloat("##v", v, sensitivity, 0.0f, 0.0f, "%.3f");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::PopID();

    return edited;
  }

  // Boxed X/Y/Z row matching the mockup: label column, then three equal-width axis fields.
  inline bool xyzGuiBoxed(const char* label, float* x, float* y, float* z, const float sensitivity = 0.1f)
  {
    rowLabel(label);

    const float gap = 6.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float w = (avail - gap * 2.0f) / 3.0f;

    bool edited = false;
    ImGui::PushID(label);
    edited |= axisField("X", theme::axisX, x, w, sensitivity);
    ImGui::SameLine(0.0f, gap);
    edited |= axisField("Y", theme::axisY, y, w, sensitivity);
    ImGui::SameLine(0.0f, gap);
    edited |= axisField("Z", theme::axisZ, z, w, sensitivity);
    ImGui::PopID();

    return edited;
  }

  // Simple vector icon set drawn with ImDrawList (stand-in for the mockup's Lucide icons; the icon
  // font is out of scope). Drawn centered in a `size`-wide box at `center`.
  enum class SecIcon { none, transform, model, rigid, collider, image, rotate, scale, pan,
                       script, scene, folder, plus, minus, light, block, rigidblock, sphere, player,
                       search };

  inline void drawSecIcon(ImDrawList* dl, const ImVec2& center, const float size, const SecIcon icon,
                          const ImU32 col)
  {
    const float r = size * 0.5f;
    switch (icon)
    {
      case SecIcon::transform: // 4-way move cross
        dl->AddLine(ImVec2(center.x, center.y - r), ImVec2(center.x, center.y + r), col, 1.6f);
        dl->AddLine(ImVec2(center.x - r, center.y), ImVec2(center.x + r, center.y), col, 1.6f);
        break;
      case SecIcon::model: // cube
        dl->AddRect(ImVec2(center.x - r * 0.8f, center.y - r * 0.8f),
                    ImVec2(center.x + r * 0.8f, center.y + r * 0.8f), col, 2.0f, 0, 1.6f);
        dl->AddLine(ImVec2(center.x - r * 0.8f, center.y), ImVec2(center.x + r * 0.8f, center.y), col, 1.2f);
        break;
      case SecIcon::image: // framed picture
        dl->AddRect(ImVec2(center.x - r, center.y - r * 0.8f),
                    ImVec2(center.x + r, center.y + r * 0.8f), col, 2.0f, 0, 1.6f);
        dl->AddCircleFilled(ImVec2(center.x - r * 0.4f, center.y - r * 0.2f), 1.6f, col);
        break;
      case SecIcon::rigid: // lightning / activity pulse
      {
        const ImVec2 pts[] = {
          { center.x - r, center.y }, { center.x - r * 0.3f, center.y },
          { center.x, center.y - r }, { center.x + r * 0.3f, center.y + r },
          { center.x + r * 0.6f, center.y }, { center.x + r, center.y }
        };
        dl->AddPolyline(pts, 6, col, 0, 1.6f);
        break;
      }
      case SecIcon::collider: // dashed sphere (approximated with a thin circle)
        dl->AddCircle(center, r * 0.9f, col, 0, 1.4f);
        break;
      case SecIcon::rotate: // circular arrow
        dl->PathArcTo(center, r * 0.85f, -1.4f, 3.6f, 16);
        dl->PathStroke(col, 0, 1.6f);
        dl->AddTriangleFilled(ImVec2(center.x + r * 0.85f, center.y - r * 0.5f),
                              ImVec2(center.x + r * 0.4f, center.y - r * 0.7f),
                              ImVec2(center.x + r * 1.05f, center.y - r * 0.05f), col);
        break;
      case SecIcon::scale: // diagonal with corner squares
        dl->AddLine(ImVec2(center.x - r * 0.7f, center.y + r * 0.7f),
                    ImVec2(center.x + r * 0.7f, center.y - r * 0.7f), col, 1.6f);
        dl->AddRectFilled(ImVec2(center.x - r * 0.9f, center.y + r * 0.45f),
                          ImVec2(center.x - r * 0.45f, center.y + r * 0.9f), col, 1.0f);
        dl->AddRect(ImVec2(center.x + r * 0.45f, center.y - r * 0.9f),
                    ImVec2(center.x + r * 0.9f, center.y - r * 0.45f), col, 1.0f, 0, 1.4f);
        break;
      case SecIcon::script: // document with code marks
      {
        dl->AddRect(ImVec2(center.x - r * 0.65f, center.y - r), ImVec2(center.x + r * 0.65f, center.y + r),
                    col, 2.0f, 0, 1.5f);
        dl->AddLine(ImVec2(center.x - r * 0.25f, center.y - r * 0.1f),
                    ImVec2(center.x - r * 0.45f, center.y + r * 0.15f), col, 1.3f);
        dl->AddLine(ImVec2(center.x - r * 0.45f, center.y + r * 0.15f),
                    ImVec2(center.x - r * 0.25f, center.y + r * 0.4f), col, 1.3f);
        dl->AddLine(ImVec2(center.x + r * 0.25f, center.y - r * 0.1f),
                    ImVec2(center.x + r * 0.45f, center.y + r * 0.15f), col, 1.3f);
        dl->AddLine(ImVec2(center.x + r * 0.45f, center.y + r * 0.15f),
                    ImVec2(center.x + r * 0.25f, center.y + r * 0.4f), col, 1.3f);
        break;
      }
      case SecIcon::scene: // stacked layers
        dl->AddQuad(ImVec2(center.x, center.y - r), ImVec2(center.x + r, center.y - r * 0.45f),
                    ImVec2(center.x, center.y + r * 0.1f), ImVec2(center.x - r, center.y - r * 0.45f),
                    col, 1.4f);
        dl->AddLine(ImVec2(center.x - r, center.y + r * 0.1f), ImVec2(center.x, center.y + r * 0.65f), col, 1.3f);
        dl->AddLine(ImVec2(center.x + r, center.y + r * 0.1f), ImVec2(center.x, center.y + r * 0.65f), col, 1.3f);
        break;
      case SecIcon::folder:
        dl->AddRect(ImVec2(center.x - r, center.y - r * 0.5f), ImVec2(center.x + r, center.y + r * 0.7f),
                    col, 1.5f, 0, 1.4f);
        dl->AddLine(ImVec2(center.x - r, center.y - r * 0.5f), ImVec2(center.x - r * 0.2f, center.y - r * 0.5f),
                    col, 1.4f);
        break;
      case SecIcon::plus:
        dl->AddLine(ImVec2(center.x, center.y - r), ImVec2(center.x, center.y + r), col, 1.8f);
        dl->AddLine(ImVec2(center.x - r, center.y), ImVec2(center.x + r, center.y), col, 1.8f);
        break;
      case SecIcon::minus:
        dl->AddLine(ImVec2(center.x - r, center.y), ImVec2(center.x + r, center.y), col, 1.8f);
        break;
      case SecIcon::light: // sun
        dl->AddCircle(center, r * 0.45f, col, 12, 1.5f);
        for (int i = 0; i < 8; ++i)
        {
          const float a = i * 3.14159265f / 4.0f;
          dl->AddLine(ImVec2(center.x + cosf(a) * r * 0.7f, center.y + sinf(a) * r * 0.7f),
                      ImVec2(center.x + cosf(a) * r, center.y + sinf(a) * r), col, 1.3f);
        }
        break;
      case SecIcon::block: // rounded square
        dl->AddRect(ImVec2(center.x - r * 0.85f, center.y - r * 0.85f),
                    ImVec2(center.x + r * 0.85f, center.y + r * 0.85f), col, 3.0f, 0, 1.6f);
        break;
      case SecIcon::rigidblock: // two offset squares
        dl->AddRect(ImVec2(center.x - r, center.y - r), ImVec2(center.x + r * 0.3f, center.y + r * 0.3f),
                    col, 2.0f, 0, 1.5f);
        dl->AddRect(ImVec2(center.x - r * 0.3f, center.y - r * 0.3f), ImVec2(center.x + r, center.y + r),
                    col, 2.0f, 0, 1.5f);
        break;
      case SecIcon::sphere:
        dl->AddCircle(center, r * 0.9f, col, 16, 1.6f);
        dl->PathArcTo(ImVec2(center.x - r * 0.2f, center.y - r * 0.3f), r * 0.7f, 0.1f, 1.3f, 8);
        dl->PathStroke(col, 0, 1.2f);
        break;
      case SecIcon::player: // head + shoulders
        dl->AddCircle(ImVec2(center.x, center.y - r * 0.4f), r * 0.4f, col, 12, 1.5f);
        dl->PathArcTo(ImVec2(center.x, center.y + r), r * 0.75f, 3.34f, 6.08f, 12);
        dl->PathStroke(col, 0, 1.5f);
        break;
      case SecIcon::search: // magnifying glass
      {
        const float ringR = r * 0.62f;
        const ImVec2 ringC(center.x - r * 0.18f, center.y - r * 0.18f);
        dl->AddCircle(ringC, ringR, col, 12, 1.5f);
        const float d = ringR * 0.70f;
        dl->AddLine(ImVec2(ringC.x + d, ringC.y + d), ImVec2(center.x + r * 0.85f, center.y + r * 0.85f),
                    col, 1.6f);
        break;
      }
      case SecIcon::pan: // 4-way arrows
      {
        dl->AddLine(ImVec2(center.x, center.y - r), ImVec2(center.x, center.y + r), col, 1.5f);
        dl->AddLine(ImVec2(center.x - r, center.y), ImVec2(center.x + r, center.y), col, 1.5f);
        const float a = r * 0.4f;
        dl->AddTriangleFilled(ImVec2(center.x, center.y - r), ImVec2(center.x - a * 0.6f, center.y - r + a),
                              ImVec2(center.x + a * 0.6f, center.y - r + a), col);
        dl->AddTriangleFilled(ImVec2(center.x, center.y + r), ImVec2(center.x - a * 0.6f, center.y + r - a),
                              ImVec2(center.x + a * 0.6f, center.y + r - a), col);
        dl->AddTriangleFilled(ImVec2(center.x - r, center.y), ImVec2(center.x - r + a, center.y - a * 0.6f),
                              ImVec2(center.x - r + a, center.y + a * 0.6f), col);
        dl->AddTriangleFilled(ImVec2(center.x + r, center.y), ImVec2(center.x + r - a, center.y - a * 0.6f),
                              ImVec2(center.x + r - a, center.y + a * 0.6f), col);
        break;
      }
      default:
        break;
    }
  }

  // Computes the drawn width of an iconPill for right-alignment before drawing it.
  inline float iconPillWidth(const char* text)
  {
    return 8.0f + 14.0f + 7.0f + ImGui::CalcTextSize(text).x + 10.0f;
  }

  // A rounded pill with a leading icon + text (the inspector's selected-object type chip). Pure
  // decoration; advances the cursor past it.
  inline void iconPill(const SecIcon icon, const char* text, const ImVec4& iconCol = theme::accent,
                       const ImVec4& textCol = theme::t2, const ImVec4& bgCol = theme::inset)
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    constexpr float iconSize = 14.0f, padL = 8.0f, padR = 10.0f, gap = 7.0f, padY = 3.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float w = padL + iconSize + gap + ts.x + padR;
    const float h = ts.y + padY * 2.0f;

    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), theme::u32(bgCol), h * 0.5f);
    drawSecIcon(dl, ImVec2(pos.x + padL + iconSize * 0.5f, pos.y + h * 0.5f), iconSize, icon,
                theme::u32(iconCol));
    dl->AddText(ImVec2(pos.x + padL + iconSize + gap, pos.y + padY), theme::u32(textCol), text);

    ImGui::Dummy(ImVec2(w, h));
  }

  // A full-width inline list row with a leading accent icon + label (e.g. the inspector's inline
  // "Add Component" list). Fills with the accent-dim wash on hover. Returns true when clicked.
  inline bool menuRow(const char* label, const SecIcon icon = SecIcon::none, const float height = 34.0f)
  {
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("##row", ImVec2(w, height));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered)
    {
      dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + height), theme::u32(theme::accdim), 6.0f);
    }

    float textX = pos.x + 11.0f;
    if (icon != SecIcon::none)
    {
      drawSecIcon(dl, ImVec2(pos.x + 18.0f, pos.y + height * 0.5f), 15.0f, icon,
                  theme::u32(hovered ? theme::accent : theme::t2));
      textX = pos.x + 34.0f;
    }
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(textX, pos.y + (height - ts.y) * 0.5f), theme::u32(theme::t1), label);

    return clicked;
  }

  // A full-width text field with an inset search glyph on the left and a placeholder hint (the asset
  // browser's "Search assets" box). The extra left frame padding clears the glyph. Returns true when
  // the text changed this frame.
  inline bool searchField(const char* id, char* buf, const size_t bufSize, const char* hint = "Search",
                          const float width = 0.0f)
  {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(34.0f, ImGui::GetStyle().FramePadding.y));
    ImGui::SetNextItemWidth(width > 0.0f ? width : ImGui::GetContentRegionAvail().x);
    const bool changed = ImGui::InputTextWithHint(id, hint, buf, bufSize);
    ImGui::PopStyleVar();

    drawSecIcon(ImGui::GetWindowDrawList(), ImVec2(pos.x + 18.0f, pos.y + h * 0.5f), 14.0f,
                SecIcon::search, theme::u32(theme::t3));

    return changed;
  }

  // A 34x34 icon button for the floating viewport tool rail. Accent-filled when `active`, subtle wash on
  // hover. Returns true when clicked.
  inline bool overlayIconButton(const char* id, const SecIcon icon, const bool active)
  {
    const float s = 34.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##b", ImVec2(s, s));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active)
    {
      dl->AddRectFilled(pos, ImVec2(pos.x + s, pos.y + s), theme::u32(theme::accent), 6.0f);
    }
    else if (hovered)
    {
      dl->AddRectFilled(pos, ImVec2(pos.x + s, pos.y + s), IM_COL32(255, 255, 255, 26), 6.0f);
    }

    const ImU32 col = active ? theme::u32(theme::onAcc) : IM_COL32(207, 211, 218, 255);
    drawSecIcon(dl, ImVec2(pos.x + s * 0.5f, pos.y + s * 0.5f), 16.0f, icon, col);

    return clicked;
  }

  // A component section header matching the mockup: rounded `head` bar, a chevron that points down when
  // open / right when collapsed, an accent icon, the title, and an optional right-aligned "-" remove
  // button. Open state is persisted in ImGui's per-window storage keyed by `label`, so callers keep the
  // same bool-returning shape as ImGui::CollapsingHeader. Sets `*removeClicked` when the "-" is pressed.
  inline bool sectionHeader(const char* label, const bool removable, bool* removeClicked,
                            const SecIcon icon = SecIcon::none)
  {
    if (removeClicked)
    {
      *removeClicked = false;
    }

    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID stateId = ImGui::GetID(label);
    bool open = storage->GetBool(stateId, true);

    const float h = 36.0f;
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::PushID(label);

    // Full-width toggle button (the remove button overlaps it on the right).
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InvisibleButton("##hdr", ImVec2(width, h)))
    {
      open = !open;
      storage->SetBool(stateId, open);
    }
    const bool hovered = ImGui::IsItemHovered();

    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h),
                      theme::u32(hovered ? theme::hover : theme::head), 7.0f);
    dl->AddRect(pos, ImVec2(pos.x + width, pos.y + h), theme::u32(theme::line), 7.0f);

    // Chevron (down when open, right when collapsed).
    const ImVec2 cc(pos.x + 18.0f, pos.y + h * 0.5f);
    const ImU32 chevCol = theme::u32(theme::t3);
    if (open)
    {
      dl->AddTriangleFilled(ImVec2(cc.x - 4.0f, cc.y - 2.5f), ImVec2(cc.x + 4.0f, cc.y - 2.5f),
                            ImVec2(cc.x, cc.y + 3.5f), chevCol);
    }
    else
    {
      dl->AddTriangleFilled(ImVec2(cc.x - 2.5f, cc.y - 4.0f), ImVec2(cc.x - 2.5f, cc.y + 4.0f),
                            ImVec2(cc.x + 3.5f, cc.y), chevCol);
    }

    // Accent icon + title.
    float textX = pos.x + 34.0f;
    if (icon != SecIcon::none)
    {
      drawSecIcon(dl, ImVec2(pos.x + 40.0f, pos.y + h * 0.5f), 14.0f, icon, theme::u32(theme::accent));
      textX = pos.x + 54.0f;
    }
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(textX, pos.y + (h - ts.y) * 0.5f), theme::u32(theme::t1), label);

    // Right-aligned remove button.
    if (removable)
    {
      const float btn = 22.0f;
      const ImVec2 bpos(pos.x + width - btn - 7.0f, pos.y + (h - btn) * 0.5f);
      ImGui::SetCursorScreenPos(bpos);
      const bool pressed = ImGui::InvisibleButton("##rm", ImVec2(btn, btn));
      const bool bhover = ImGui::IsItemHovered();
      if (bhover)
      {
        dl->AddRectFilled(bpos, ImVec2(bpos.x + btn, bpos.y + btn), theme::u32(theme::accdim), 5.0f);
      }
      const ImU32 minusCol = theme::u32(bhover ? theme::danger : theme::t3);
      dl->AddLine(ImVec2(bpos.x + 6.0f, bpos.y + btn * 0.5f),
                  ImVec2(bpos.x + btn - 6.0f, bpos.y + btn * 0.5f), minusCol, 1.8f);
      if (pressed && removeClicked)
      {
        *removeClicked = true;
      }
    }

    ImGui::PopID();

    // Advance the cursor past the header and add a little breathing room before the body.
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h));
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    return open;
  }

  // A labelled single-value drag field: muted label in the left column, framed value box filling the
  // rest of the row (the mockup's Gravity / Radius rows). Returns true if edited.
  inline bool labeledDrag(const char* label, float* v, const float speed = 0.1f)
  {
    rowLabel(label);

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    const bool edited = ImGui::DragFloat("##v", v, speed);
    ImGui::PopID();

    return edited;
  }

  // An accent track slider with a glowing thumb (Friction / Mass / Scale All in the mockup).
  // Label sits in the left column; the value is drawn right-aligned inside the track.
  inline bool accentSlider(const char* label, float* v, const float min, const float max)
  {
    rowLabel(label);

    const float width = ImGui::GetContentRegionAvail().x;
    const float h = 28.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(label);
    ImGui::InvisibleButton("##slider", ImVec2(width, h));
    const bool active = ImGui::IsItemActive();
    ImGui::PopID();

    bool edited = false;
    if (active)
    {
      const float mouseX = ImGui::GetIO().MousePos.x;
      float t = (mouseX - pos.x) / width;
      t = std::clamp(t, 0.0f, 1.0f);
      const float nv = min + (max - min) * t;
      if (nv != *v)
      {
        *v = nv;
        edited = true;
      }
    }

    const float t = std::clamp((*v - min) / (max - min), 0.0f, 1.0f);
    const float cy = pos.y + h * 0.5f;
    const float trackH = 6.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Track + filled portion.
    dl->AddRectFilled(ImVec2(pos.x, cy - trackH * 0.5f), ImVec2(pos.x + width, cy + trackH * 0.5f),
                      theme::u32(theme::inset), trackH * 0.5f);
    dl->AddRect(ImVec2(pos.x, cy - trackH * 0.5f), ImVec2(pos.x + width, cy + trackH * 0.5f),
                theme::u32(theme::line), trackH * 0.5f);
    const float fillX = pos.x + width * t;
    dl->AddRectFilled(ImVec2(pos.x, cy - trackH * 0.5f), ImVec2(fillX, cy + trackH * 0.5f),
                      theme::u32(theme::accent), trackH * 0.5f);

    // Thumb with accent glow ring.
    dl->AddCircleFilled(ImVec2(fillX, cy), 9.0f, theme::u32(theme::accdim));
    dl->AddCircleFilled(ImVec2(fillX, cy), 7.0f, theme::u32(theme::accent));

    // Value, right-aligned inside the track.
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", *v);
    const ImVec2 vts = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(pos.x + width - vts.x - 10.0f, cy - vts.y * 0.5f), theme::u32(theme::t1), buf);

    return edited;
  }

  // ------------------------------------------------------------------------------------------------
  // Asset widgets (asset grid card + model-renderer reference row)
  // ------------------------------------------------------------------------------------------------

  // Truncates `text` with a trailing ellipsis so it fits within `maxWidth` at the current font.
  inline std::string ellipsize(const char* text, const float maxWidth)
  {
    if (ImGui::CalcTextSize(text).x <= maxWidth)
    {
      return text;
    }

    std::string s = text;
    while (!s.empty() && ImGui::CalcTextSize((s + "...").c_str()).x > maxWidth)
    {
      s.pop_back();
    }
    return s + "...";
  }

  // Draws a thumbnail into [p0,p1]: the texture image when `thumb != 0`, otherwise a centered type icon
  // on a faint tinted backdrop.
  inline void drawThumb(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, const ImTextureID thumb,
                        const SecIcon icon, const ImVec4& iconCol, const float rounding)
  {
    if (thumb != 0)
    {
      dl->AddImageRounded(thumb, p0, p1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding);
      return;
    }

    ImVec4 tint = iconCol;
    tint.w = 0.14f;
    dl->AddRectFilled(p0, p1, theme::u32(tint), rounding);
    const ImVec2 center((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    drawSecIcon(dl, center, (p1.y - p0.y) * 0.42f, icon, theme::u32(iconCol));
  }

  // A square asset grid tile (thumbnail/icon + type badge). Submits an InvisibleButton as the tile, so
  // the caller can attach drag-drop / double-click handling to the last item immediately after. Returns
  // true when clicked. Draw the filename underneath with assetCardLabel().
  inline bool assetCard(const float size, const ImTextureID thumb, const SecIcon icon,
                        const ImVec4& iconCol, const char* kind, const ImVec4& kindCol,
                        const bool selected = false)
  {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##tile", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(pos.x + size, pos.y + size);

    dl->AddRectFilled(pos, p1, theme::u32(theme::inset), 8.0f);
    drawThumb(dl, pos, p1, thumb, icon, iconCol, 8.0f);
    // Selected tiles get a solid 2px accent ring; otherwise hover tints the border and the rest sit on
    // the faint line color.
    if (selected)
    {
      dl->AddRect(pos, p1, theme::u32(theme::accent), 8.0f, 0, 2.0f);
    }
    else
    {
      dl->AddRect(pos, p1, theme::u32(hovered ? theme::accent : theme::line), 8.0f);
    }

    // Type badge (color dot + kind) top-left.
    const ImVec2 ts = ImGui::CalcTextSize(kind);
    const float dot = 7.0f, padL = 6.0f, gap = 5.0f, padR = 8.0f, bh = ts.y + 4.0f;
    const ImVec2 b0(pos.x + 7.0f, pos.y + 7.0f);
    const ImVec2 b1(b0.x + padL + dot + gap + ts.x + padR, b0.y + bh);
    dl->AddRectFilled(b0, b1, IM_COL32(13, 14, 16, 180), bh * 0.5f);
    dl->AddCircleFilled(ImVec2(b0.x + padL + dot * 0.5f, (b0.y + b1.y) * 0.5f), dot * 0.5f, theme::u32(kindCol));
    dl->AddText(ImVec2(b0.x + padL + dot + gap, b0.y + 2.0f), IM_COL32(207, 211, 218, 255), kind);

    return clicked;
  }

  // The filename label drawn under an assetCard, ellipsized to the tile width. Sits directly under the
  // tile on the standard item gap (the mockup's ~9px thumbnail-to-name spacing).
  inline void assetCardLabel(const char* name, const float width)
  {
    ImGui::TextColored(theme::t1, "%s", ellipsize(name, width).c_str());
    ImGui::Spacing();
  }

  // A full-width model-renderer reference slot (mockup: 30px thumb + kind label + filename + folder
  // glyph). Submits an InvisibleButton spanning the row, so the caller can wrap it in a drag-drop
  // target. Returns true when clicked.
  inline bool assetRefRow(const char* id, const char* kind, const char* name, const ImTextureID thumb,
                          const SecIcon icon, const ImVec4& iconCol)
  {
    const float h = 58.0f;
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##row", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(pos.x + w, pos.y + h);
    dl->AddRectFilled(pos, p1, theme::u32(theme::inset), 7.0f);
    dl->AddRect(pos, p1, theme::u32(hovered ? theme::line2 : theme::line), 7.0f);

    // Thumbnail box.
    const float tb = 42.0f, pad = 9.0f;
    const ImVec2 t0(pos.x + pad, pos.y + (h - tb) * 0.5f);
    const ImVec2 t1(t0.x + tb, t0.y + tb);
    drawThumb(dl, t0, t1, thumb, icon, iconCol, 5.0f);

    // Folder glyph on the right.
    const float folderR = 8.0f;
    const ImVec2 fc(p1.x - pad - folderR, pos.y + h * 0.5f);
    drawSecIcon(dl, fc, folderR * 2.0f, SecIcon::folder, theme::u32(theme::t3));

    // Kind (muted) over filename.
    const float tx = t1.x + 12.0f;
    const float nameMax = (fc.x - folderR - 8.0f) - tx;
    dl->AddText(ImVec2(tx, pos.y + 13.0f), theme::u32(theme::t3), kind);
    dl->AddText(ImVec2(tx, pos.y + 31.0f), theme::u32(theme::t1), ellipsize(name, nameMax).c_str());

    return clicked;
  }

  // A compact accent checkbox that sizes to its own content (box + label), so several can sit on one row
  // with SameLine (the asset browser's filter chips). Returns true when toggled.
  inline bool accentCheckboxCompact(const char* label, bool* v)
  {
    const float box = 17.0f;
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float h = std::max(box, ImGui::GetFrameHeight());
    const float w = box + 8.0f + ts.x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(label);
    const bool pressed = ImGui::InvisibleButton("##cb", ImVec2(w, h));
    ImGui::PopID();
    if (pressed)
    {
      *v = !*v;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 boxMin(pos.x, pos.y + (h - box) * 0.5f);
    const ImVec2 boxMax(boxMin.x + box, boxMin.y + box);
    dl->AddRectFilled(boxMin, boxMax, theme::u32(*v ? theme::accent : theme::inset), 5.0f);
    if (!*v)
    {
      dl->AddRect(boxMin, boxMax, theme::u32(theme::line2), 5.0f);
    }
    else
    {
      const ImU32 mark = theme::u32(theme::onAcc);
      dl->AddLine(ImVec2(boxMin.x + box * 0.26f, boxMin.y + box * 0.52f),
                  ImVec2(boxMin.x + box * 0.44f, boxMin.y + box * 0.70f), mark, 2.2f);
      dl->AddLine(ImVec2(boxMin.x + box * 0.44f, boxMin.y + box * 0.70f),
                  ImVec2(boxMin.x + box * 0.76f, boxMin.y + box * 0.32f), mark, 2.2f);
    }
    dl->AddText(ImVec2(boxMax.x + 8.0f, pos.y + (h - ts.y) * 0.5f), theme::u32(theme::t1), label);

    return pressed;
  }

  // ------------------------------------------------------------------------------------------------
  // Buttons + empty states
  // ------------------------------------------------------------------------------------------------

  // A small square icon button for inline row actions (+/- on object rows). Accent wash on hover,
  // or a red wash when `danger`. `h` defaults to `w` (square); pass the row's height so the hit/wash
  // area fills the full row instead of leaving dead space above/below. Returns true when clicked.
  inline bool rowIconButton(const char* id, const SecIcon icon, const bool danger, const float w = 27.0f,
                             const float h = -1.0f)
  {
    const float height = h > 0.0f ? h : w;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##b", ImVec2(w, height));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered)
    {
      dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + height),
                        theme::u32(danger ? theme::v4(58, 35, 38) : theme::accdim), 5.0f);
    }
    const ImVec4 iconCol = hovered ? (danger ? theme::danger : theme::accent) : theme::t3;
    const float iconSize = (w < height ? w : height) * 0.48f;
    drawSecIcon(dl, ImVec2(pos.x + w * 0.5f, pos.y + height * 0.5f), iconSize, icon, theme::u32(iconCol));

    return clicked;
  }

  // Draws a dashed rounded-less rectangle border around [p0,p1].
  inline void drawDashedRect(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, const ImU32 col,
                             const float dash = 5.0f, const float gap = 4.0f, const float thickness = 1.0f)
  {
    const auto edge = [&](const ImVec2& a, const ImVec2& b) {
      const float dx = b.x - a.x, dy = b.y - a.y;
      const float len = std::sqrt(dx * dx + dy * dy);
      if (len <= 0.0f)
      {
        return;
      }
      const float ux = dx / len, uy = dy / len;
      for (float t = 0.0f; t < len; t += dash + gap)
      {
        const float e = std::min(t + dash, len);
        dl->AddLine(ImVec2(a.x + ux * t, a.y + uy * t), ImVec2(a.x + ux * e, a.y + uy * e), col, thickness);
      }
    };
    edge(p0, ImVec2(p1.x, p0.y));
    edge(ImVec2(p1.x, p0.y), p1);
    edge(p1, ImVec2(p0.x, p1.y));
    edge(ImVec2(p0.x, p1.y), p0);
  }

  // Centers an optional icon + label inside [pos, pos+size] in `col`.
  inline void drawCenteredIconLabel(ImDrawList* dl, const ImVec2& pos, const ImVec2& size,
                                    const SecIcon icon, const char* label, const ImU32 col)
  {
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float iconW = icon != SecIcon::none ? 22.0f : 0.0f;
    const float total = iconW + ts.x;
    float x = pos.x + (size.x - total) * 0.5f;
    const float cy = pos.y + size.y * 0.5f;
    if (icon != SecIcon::none)
    {
      drawSecIcon(dl, ImVec2(x + 8.0f, cy), 16.0f, icon, col);
      x += iconW;
    }
    dl->AddText(ImVec2(x, cy - ts.y * 0.5f), col, label);
  }

  // A full-width accent button (accent-dim fill + accent border/text, matching the mockup's "Create New
  // Object"). Returns true when clicked.
  inline bool accentButton(const char* label, const SecIcon icon = SecIcon::none, const float height = 40.0f)
  {
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    const bool clicked = ImGui::InvisibleButton(label, ImVec2(w, height));
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(pos.x + w, pos.y + height);
    dl->AddRectFilled(pos, p1, theme::u32(hovered ? theme::accSoft : theme::accdim), 7.0f);
    dl->AddRect(pos, p1, theme::u32(theme::accSoft), 7.0f);
    drawCenteredIconLabel(dl, pos, ImVec2(w, height), icon, label, theme::u32(theme::accent));

    return clicked;
  }

  // A full-width dashed accent button (the mockup's "Add Component"). Returns true when clicked.
  inline bool dashedButton(const char* label, const SecIcon icon = SecIcon::none, const float height = 40.0f)
  {
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    const bool clicked = ImGui::InvisibleButton(label, ImVec2(w, height));
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(pos.x + w, pos.y + height);
    dl->AddRectFilled(pos, p1, theme::u32(hovered ? theme::accdim : theme::v4(31, 184, 212, 22)), 7.0f);
    drawDashedRect(dl, pos, p1, theme::u32(theme::accSoft), 6.0f, 4.0f, 1.2f);
    drawCenteredIconLabel(dl, pos, ImVec2(w, height), icon, label, theme::u32(theme::accent));

    return clicked;
  }

  // A dashed empty-state box with centered muted text (the mockup's "No scripts attached").
  inline void dashedBox(const char* text, const float height = 60.0f)
  {
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, height));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(pos.x + w, pos.y + height);
    drawDashedRect(dl, pos, p1, theme::u32(theme::line2), 5.0f, 4.0f, 1.0f);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddText(ImVec2(pos.x + (w - ts.x) * 0.5f, pos.y + (height - ts.y) * 0.5f), theme::u32(theme::t3), text);
  }

  // A centered "all clear" empty state (the mockup's "No problems detected"): a green check inside a
  // tinted circle over muted text. Fills the available content region and centers the block within it.
  inline void successEmptyState(const char* text)
  {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    constexpr float circleR = 23.0f;
    constexpr float gap = 12.0f;
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float blockH = circleR * 2.0f + gap + ts.y;

    const float cx = origin.x + avail.x * 0.5f;
    float y = origin.y + std::max(0.0f, (avail.y - blockH) * 0.5f);

    // Tinted circle + check mark.
    const ImVec2 cc(cx, y + circleR);
    ImVec4 tint = theme::sceneGreen;
    tint.w = 0.15f;
    dl->AddCircleFilled(cc, circleR, theme::u32(tint));
    const ImU32 mark = theme::u32(theme::sceneGreen);
    const float s = circleR;
    dl->AddLine(ImVec2(cc.x - s * 0.32f, cc.y + s * 0.02f), ImVec2(cc.x - s * 0.06f, cc.y + s * 0.28f), mark, 2.6f);
    dl->AddLine(ImVec2(cc.x - s * 0.06f, cc.y + s * 0.28f), ImVec2(cc.x + s * 0.36f, cc.y - s * 0.22f), mark, 2.6f);

    y += circleR * 2.0f + gap;
    dl->AddText(ImVec2(cx - ts.x * 0.5f, y), theme::u32(theme::t2), text);
  }

  // A centered neutral empty state: a muted icon in a subtle disc over a primary line and an optional
  // secondary line (e.g. the inspector's "No object selected"). Fills the available content region.
  inline void emptyState(const SecIcon icon, const char* text, const char* subtext = nullptr)
  {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    constexpr float circleR = 24.0f;
    constexpr float gap = 13.0f;
    constexpr float subGap = 5.0f;
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 ss = subtext ? ImGui::CalcTextSize(subtext) : ImVec2(0.0f, 0.0f);
    float blockH = circleR * 2.0f + gap + ts.y;
    if (subtext)
    {
      blockH += subGap + ss.y;
    }

    const float cx = origin.x + avail.x * 0.5f;
    float y = origin.y + std::max(0.0f, (avail.y - blockH) * 0.5f);

    const ImVec2 cc(cx, y + circleR);
    dl->AddCircleFilled(cc, circleR, theme::u32(theme::head));
    drawSecIcon(dl, cc, circleR * 0.9f, icon, theme::u32(theme::t3));

    y += circleR * 2.0f + gap;
    dl->AddText(ImVec2(cx - ts.x * 0.5f, y), theme::u32(theme::t2), text);
    if (subtext)
    {
      y += ts.y + subGap;
      dl->AddText(ImVec2(cx - ss.x * 0.5f, y), theme::u32(theme::t3), subtext);
    }
  }
}

#endif //ECS3D_GUICOMPONENTS_H
