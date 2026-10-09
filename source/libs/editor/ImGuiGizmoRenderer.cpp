#include "ImGuiGizmoRenderer.h"
#include "EditorTheme.h"
#include <imgui.h>

namespace {
  ImVec4 mixToward(const ImVec4& color, const ImVec4& target, const float t)
  {
    return {
      color.x + (target.x - color.x) * t,
      color.y + (target.y - color.y) * t,
      color.z + (target.z - color.z) * t,
      color.w + (target.w - color.w) * t
    };
  }

  const ImVec4& axisColor(const gizmo::Handle handle)
  {
    switch (handle)
    {
      case gizmo::Handle::x:
        return theme::axisX;
      case gizmo::Handle::y:
        return theme::axisY;
      case gizmo::Handle::z:
        return theme::axisZ;
      default:
        return theme::t1; // uniform
    }
  }

  ImU32 colorFor(const gizmo::Handle handle, const gizmo::Highlight highlight)
  {
    if (highlight == gizmo::Highlight::active)
    {
      return theme::u32(theme::accent);
    }

    const ImVec4& base = axisColor(handle);

    if (highlight == gizmo::Highlight::hovered)
    {
      return theme::u32(mixToward(base, ImVec4(1.0f, 1.0f, 1.0f, base.w), 0.4f));
    }

    return theme::u32(base);
  }
}

void ImGuiGizmoRenderer::setDrawList(ImDrawList* drawList)
{
  m_drawList = drawList;
}

void ImGuiGizmoRenderer::draw(const gizmo::Frame& frame, const gizmo::View& view)
{
  if (!m_drawList)
  {
    return;
  }

  const auto drawTriangle = [&](const gizmo::Triangle& triangle)
  {
    const auto a = gizmo::project(view, triangle.a);
    const auto b = gizmo::project(view, triangle.b);
    const auto c = gizmo::project(view, triangle.c);

    if (!a || !b || !c)
    {
      return;
    }

    m_drawList->AddTriangleFilled(ImVec2(a->x, a->y), ImVec2(b->x, b->y), ImVec2(c->x, c->y),
                                  colorFor(triangle.handle, triangle.highlight));
  };

  const auto drawLine = [&](const gizmo::Line& line)
  {
    const auto a = gizmo::project(view, line.a);
    const auto b = gizmo::project(view, line.b);

    if (!a || !b)
    {
      return;
    }

    m_drawList->AddLine(ImVec2(a->x, a->y), ImVec2(b->x, b->y), colorFor(line.handle, line.highlight),
                        line.thicknessPixels);
  };

  const ImVec2 clipMin(view.viewport.x, view.viewport.y);
  const ImVec2 clipMax(view.viewport.x + view.viewport.width, view.viewport.y + view.viewport.height);
  m_drawList->PushClipRect(clipMin, clipMax, true);

  // Highlight::none primitives first, then hovered/active on top, so the active handle always reads
  // clearly over the rest of the gizmo.
  for (const auto& triangle : frame.triangles)
  {
    if (triangle.highlight == gizmo::Highlight::none)
    {
      drawTriangle(triangle);
    }
  }

  for (const auto& line : frame.lines)
  {
    if (line.highlight == gizmo::Highlight::none)
    {
      drawLine(line);
    }
  }

  for (const auto& triangle : frame.triangles)
  {
    if (triangle.highlight != gizmo::Highlight::none)
    {
      drawTriangle(triangle);
    }
  }

  for (const auto& line : frame.lines)
  {
    if (line.highlight != gizmo::Highlight::none)
    {
      drawLine(line);
    }
  }

  m_drawList->PopClipRect();
}
