#ifndef GIZMOEDITSENDING_H
#define GIZMOEDITSENDING_H

#include "Gizmo.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <uuid.h>

class Component;
class Transform;

// Turns the viewport gizmo's drag frames into edits, the way the Inspector reports a slider drag: a per-frame
// edit while the Transform changes, and one committed before/after edit once the drag ends. A drag that never
// changed the Transform, such as a click on a handle, sends nothing. Changed means Transform::serialize()
// differs, the same comparison EditorApp uses to drop a no-op committed edit.
class GizmoEditSending {
public:
  using EditCallback = std::function<void(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component)>;
  using EditCommittedCallback = std::function<void(const uuids::uuid& objectUUID, const nlohmann::json& before,
                                                    const nlohmann::json& after)>;

  void setEditCallback(EditCallback callback);

  void setEditCommittedCallback(EditCommittedCallback callback);

  // Applies the frame's local pose to transform, which belongs to target, and sends what the frame calls for.
  void apply(const gizmo::Frame& frame, const uuids::uuid& target, const std::shared_ptr<Transform>& transform);

  // Ends an in-flight drag without restoring anything. transform is the drag target's Transform, or null when
  // the target is gone, in which case nothing is committed.
  void abandon(const std::shared_ptr<Transform>& transform);

  [[nodiscard]] const std::optional<uuids::uuid>& dragTarget() const;

private:
  EditCallback m_editCallback;
  EditCommittedCallback m_editCommittedCallback;

  // The in-flight drag's target and its Transform::serialize() when the drag started.
  std::optional<uuids::uuid> m_dragTarget;
  nlohmann::json m_dragBefore;

  void finishDrag(const std::shared_ptr<Transform>& transform);
};

#endif  // GIZMOEDITSENDING_H
