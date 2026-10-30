#include "ViewportGizmo.h"
#include "ImGuiGizmoRenderer.h"
#include <objects/ObjectManager.h>
#include <objects/Object.h>
#include <objects/components/Component.h>
#include <objects/components/Transform.h>
#include <imgui.h>
#include <utility>

namespace {
  [[nodiscard]] std::shared_ptr<Transform> transformFor(ObjectManager* objectManager,
                                                        const std::optional<uuids::uuid>& target)
  {
    if (!objectManager || !target)
    {
      return nullptr;
    }

    const auto object = objectManager->getObjectByUUID(*target);
    return object ? object->getComponent<Transform>(ComponentType::transform) : nullptr;
  }

  [[nodiscard]] gizmo::Pose localPoseOf(const std::shared_ptr<Transform>& transform)
  {
    return { transform->getLocalPosition(), transform->getLocalRotation(), transform->getLocalScale() };
  }

  [[nodiscard]] gizmo::Pose worldPoseOf(const std::shared_ptr<Transform>& transform)
  {
    return { transform->getPosition(), transform->getRotation(), transform->getScale() };
  }

  [[nodiscard]] gizmo::ParentFrame parentFrameOf(const Object& object)
  {
    gizmo::ParentFrame frame;
    const auto parent = object.getParent();
    const auto parentTransform = parent ? parent->getComponent<Transform>(ComponentType::transform) : nullptr;

    if (parentTransform)
    {
      frame.position = parentTransform->getPosition();
      frame.orientation = glm::quat(glm::radians(parentTransform->getRotation()));
      frame.scale = parentTransform->getScale();
    }

    return frame;
  }

  [[nodiscard]] bool insideViewport(const gizmo::Rect& viewport, const glm::vec2 mouse)
  {
    return mouse.x >= viewport.x && mouse.x <= viewport.x + viewport.width
      && mouse.y >= viewport.y && mouse.y <= viewport.y + viewport.height;
  }
}

ViewportGizmo::ViewportGizmo()
  : m_renderer(std::make_unique<ImGuiGizmoRenderer>())
{}

ViewportGizmo::~ViewportGizmo() = default;

void ViewportGizmo::setEditCallback(EditCallback callback)
{
  m_editSending.setEditCallback(std::move(callback));
}

void ViewportGizmo::setEditCommittedCallback(EditCommittedCallback callback)
{
  m_editSending.setEditCommittedCallback(std::move(callback));
}

bool ViewportGizmo::capturesMouseAt(const glm::vec2 mouse) const
{
  if (m_state.dragging)
  {
    return true;
  }

  if (!m_lastInput)
  {
    return false;
  }

  gizmo::State stateCopy = m_state;
  gizmo::Input inputCopy = *m_lastInput;
  inputCopy.mouse = mouse;
  // Last frame's mouseOverView was taken at last frame's cursor; a cursor that just jumped onto a handle
  // from outside the scene would otherwise read as not hovering it.
  inputCopy.mouseOverView = insideViewport(inputCopy.view.viewport, mouse);
  inputCopy.mouseDown = false;
  stateCopy.mouseDownLastFrame = false;

  const gizmo::Frame frame = gizmo::update(stateCopy, inputCopy);
  return frame.hovered != gizmo::Handle::none;
}

gizmo::Mode ViewportGizmo::mode() const
{
  return m_state.mode;
}

void ViewportGizmo::setMode(const gizmo::Mode mode)
{
  m_state.mode = mode;
}

gizmo::Space ViewportGizmo::space() const
{
  return m_state.space;
}

void ViewportGizmo::setSpace(const gizmo::Space space)
{
  m_state.space = space;
}

void ViewportGizmo::abandonDrag(ObjectManager* objectManager)
{
  const auto target = m_editSending.dragTarget();
  if (!target)
  {
    return;
  }

  gizmo::cancel(m_state);
  m_editSending.abandon(transformFor(objectManager, target));
}

void ViewportGizmo::update(ObjectManager* objectManager, const std::optional<uuids::uuid> target,
                           const gizmo::View& view, ImDrawList* drawList, const bool editable,
                           const gizmo::Snap& snap, const bool sceneHovered)
{
  const auto transform = transformFor(objectManager, target);
  const bool targetChanged = m_editSending.dragTarget().has_value() && target != m_editSending.dragTarget();

  if (!transform || !editable || targetChanged)
  {
    abandonDrag(objectManager);
    m_lastInput.reset();
    return;
  }

  const auto& io = ImGui::GetIO();
  const bool mouseInsideViewport = insideViewport(view.viewport, { io.MousePos.x, io.MousePos.y });

  gizmo::Input input;
  input.view = view;
  input.mouse = { io.MousePos.x, io.MousePos.y };
  input.mouseDown = io.MouseDown[0];
  input.mouseOverView = mouseInsideViewport && sceneHovered;
  input.suppressSnap = io.KeyAlt;
  input.snap = snap;
  input.local = localPoseOf(transform);
  input.world = worldPoseOf(transform);
  input.parent = parentFrameOf(*transform->getOwner());

  const gizmo::Frame frame = gizmo::update(m_state, input);

  m_lastInput = input;

  if (drawList)
  {
    m_renderer->setDrawList(drawList);
    m_renderer->draw(frame, view);
  }

  m_editSending.apply(frame, *target, transform);
}
