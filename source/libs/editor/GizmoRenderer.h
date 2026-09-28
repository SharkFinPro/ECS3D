#ifndef GIZMORENDERER_H
#define GIZMORENDERER_H

#include "Gizmo.h"

// Backend-neutral drawing sink for a gizmo::Frame: the headless gizmo core outputs world-space Line/
// Triangle primitives and this projects + draws them however its backend wants. ImGuiGizmoRenderer is
// the only implementation today; the interface exists so a later native overlay pass can replace it with
// no change to ViewportGizmo or Gizmo.h.
class GizmoRenderer {
public:
  virtual ~GizmoRenderer() = default;

  virtual void draw(const gizmo::Frame& frame, const gizmo::View& view) = 0;
};

#endif //GIZMORENDERER_H
