#include "GizmoEditSending.h"
#include <objects/components/Transform.h>
#include <utility>

void GizmoEditSending::setEditCallback(EditCallback callback)
{
  m_editCallback = std::move(callback);
}

void GizmoEditSending::setEditCommittedCallback(EditCommittedCallback callback)
{
  m_editCommittedCallback = std::move(callback);
}

const std::optional<uuids::uuid>& GizmoEditSending::dragTarget() const
{
  return m_dragTarget;
}

void GizmoEditSending::apply(const gizmo::Frame& frame, const uuids::uuid& target,
                             const std::shared_ptr<Transform>& transform)
{
  if (frame.dragStarted)
  {
    m_dragTarget = target;
    m_dragBefore = transform->serialize();
  }

  if (frame.local)
  {
    const auto previous = transform->serialize();

    transform->setPosition(frame.local->position);
    transform->setRotation(frame.local->rotation);
    transform->setScale(frame.local->scale);

    if (m_editCallback && transform->serialize() != previous)
    {
      m_editCallback(target, transform);
    }
  }

  if (frame.dragFinished)
  {
    finishDrag(transform);
  }
}

void GizmoEditSending::abandon(const std::shared_ptr<Transform>& transform)
{
  finishDrag(transform);
}

void GizmoEditSending::finishDrag(const std::shared_ptr<Transform>& transform)
{
  if (!m_dragTarget)
  {
    return;
  }

  const auto target = *m_dragTarget;
  const auto before = std::move(m_dragBefore);

  m_dragTarget.reset();
  m_dragBefore = nlohmann::json();

  if (!m_editCommittedCallback || !transform)
  {
    return;
  }

  const auto after = transform->serialize();
  if (after != before)
  {
    m_editCommittedCallback(target, before, after);
  }
}
