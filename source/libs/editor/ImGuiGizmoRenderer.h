#ifndef IMGUIGIZMORENDERER_H
#define IMGUIGIZMORENDERER_H

#include "GizmoRenderer.h"

struct ImDrawList;

// Draws a gizmo::Frame into the editor's viewport via ImGui's draw list: projects every world-space
// vertex with gizmo::project and skips a primitive if any vertex does not project (behind the camera or
// a degenerate viewport).
class ImGuiGizmoRenderer final : public GizmoRenderer {
public:
  // The draw list to draw into this frame - the scene overlay callback hands over a different one call
  // to call (and a different window's list entirely in full-window mode), so this is set fresh each time.
  void setDrawList(ImDrawList* drawList);

  void draw(const gizmo::Frame& frame, const gizmo::View& view) override;

private:
  ImDrawList* m_drawList = nullptr;
};

#endif //IMGUIGIZMORENDERER_H
