#ifndef VIEWPORTGIZMO_H
#define VIEWPORTGIZMO_H

#include "Gizmo.h"
#include <nlohmann/json_fwd.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <uuid.h>

struct ImDrawList;
class ObjectManager;
class Component;
class ImGuiGizmoRenderer;

// Drives the viewport translate/rotate/scale gizmo for the editor's current selection: builds a
// gizmo::Input from the target's Transform each frame, runs the headless gizmo::update, draws the result
// through an ImGuiGizmoRenderer, and reports a drag the same way the Inspector reports a slider drag - a
// per-frame componentEdit while it moves, one committed before/after edit once it ends.
class ViewportGizmo {
public:
  using EditCallback = std::function<void(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component)>;
  using EditCommittedCallback = std::function<void(const uuids::uuid& objectUUID, const nlohmann::json& before,
                                                    const nlohmann::json& after)>;

  ViewportGizmo();
  ~ViewportGizmo();

  void setEditCallback(EditCallback callback);

  void setEditCommittedCallback(EditCommittedCallback callback);

  // objectManager/target/drawList may be null/nullopt (no scene loaded yet, nothing selected, an asset
  // selected). editable false or an unusable target (missing object, no Transform) ends any in-flight
  // drag as if the target had vanished - restoring nothing, since the per-frame sends already reached the
  // server, but committing whatever the drag did so far when the target object still exists. A target
  // change mid-drag counts as the old target going away the same way.
  void update(ObjectManager* objectManager, std::optional<uuids::uuid> target, const gizmo::View& view,
             ImDrawList* drawList, bool editable, const gizmo::Snap& snap, bool sceneHovered);

  // True while a drag is active; otherwise re-runs the headless core against last frame's target/state
  // with mouse moved to the given position (button up), so the caller can ask "would this click land on
  // a handle" for a mouse that moved onto one and pressed in the same frame this class doesn't see until
  // its own update() runs next. No side effects - never touches the real state or fires a callback. False
  // if the gizmo drew nothing last frame (no target, not editable, ...).
  [[nodiscard]] bool capturesMouseAt(glm::vec2 mouse) const;

  [[nodiscard]] gizmo::Mode mode() const;
  void setMode(gizmo::Mode mode);

  [[nodiscard]] gizmo::Space space() const;
  void setSpace(gizmo::Space space);

private:
  gizmo::State m_state;
  std::unique_ptr<ImGuiGizmoRenderer> m_renderer;

  EditCallback m_editCallback;
  EditCommittedCallback m_editCommittedCallback;

  // The in-flight drag's target and its Transform::serialize() before the drag started, so an abandoned
  // drag (see update()) can still report the committed edit's before/after pair. Set on dragStarted,
  // cleared once the drag ends one way or another.
  std::optional<uuids::uuid> m_dragTarget;
  std::string m_dragBefore;

  // The gizmo::Input update() last built, kept so capturesMouseAt can re-test a hover without the caller
  // having to rebuild a View/Snap it doesn't otherwise need. Reset whenever update() draws nothing.
  std::optional<gizmo::Input> m_lastInput;

  // Ends an in-flight drag (if any) without restoring anything: reports the committed edit against the
  // old target's current state when it still exists and has a Transform, otherwise just drops it.
  void abandonDrag(ObjectManager* objectManager);
};

#endif //VIEWPORTGIZMO_H
