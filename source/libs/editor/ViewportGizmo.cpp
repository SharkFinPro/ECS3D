#include "ViewportGizmo.h"
#include "ImGuiGizmoRenderer.h"
#include <objects/ObjectManager.h>
#include <objects/Object.h>
#include <objects/components/Component.h>
#include <objects/components/Transform.h>
#include <nlohmann/json.hpp>
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
  m_editCallback = std::move(callback);
}

void ViewportGizmo::setEditCommittedCallback(EditCommittedCallback callback)
{
  m_editCommittedCallback = std::move(callback);
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
  if (!m_dragTarget)
  {
    return;
  }

  const auto target = *m_dragTarget;
  const auto before = m_dragBefore;

  m_dragTarget.reset();
  m_dragBefore.clear();
  gizmo::cancel(m_state);

  if (!m_editCommittedCallback)
  {
    return;
  }

  const auto transform = transformFor(objectManager, target);
  if (!transform)
  {
    return;
  }

  auto beforeJson = nlohmann::json::parse(before, nullptr, false);
  if (beforeJson.is_discarded())
  {
    return;
  }

  m_editCommittedCallback(target, beforeJson, transform->serialize());
}

void ViewportGizmo::update(ObjectManager* objectManager, const std::optional<uuids::uuid> target,
                           const gizmo::View& view, ImDrawList* drawList, const bool editable,
                           const gizmo::Snap& snap, const bool sceneHovered)
{
  const auto transform = transformFor(objectManager, target);
  const bool targetChanged = m_dragTarget.has_value() && target != m_dragTarget;

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

  if (frame.dragStarted)
  {
    m_dragTarget = target;
    m_dragBefore = transform->serialize().dump();
  }

  if (frame.local)
  {
    transform->setPosition(frame.local->position);
    transform->setRotation(frame.local->rotation);
    transform->setScale(frame.local->scale);

    if (m_editCallback)
    {
      m_editCallback(*target, transform);
    }
  }

  if (frame.dragFinished)
  {
    if (m_editCommittedCallback)
    {
      auto beforeJson = nlohmann::json::parse(m_dragBefore, nullptr, false);
      if (!beforeJson.is_discarded())
      {
        m_editCommittedCallback(*target, beforeJson, transform->serialize());
      }
    }

    m_dragTarget.reset();
    m_dragBefore.clear();
  }
}
