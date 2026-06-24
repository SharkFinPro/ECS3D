#include "ColliderEditor.h"
#include "../ComponentEditor.h"
#include <memory>

class Component;

void registerColliderEditors(ComponentEditor& componentEditor)
{
  // TODO: settle the ComponentEditor key scheme. Colliders both serialize as type "Collider" but
  // TODO:   differ by subType, so they register under their display names here ("Box Collider" /
  // TODO:   "Sphere Collider"). Transform/RigidBody/etc. currently register under their serialize type
  // TODO:   string. Pick one scheme (display name or a stable component key) and make the caller match.
  componentEditor.registerHandler("Box Collider", [](const std::shared_ptr<Component>& component) {
    // TODO: migrate BoxCollider::displayGui here: cast to BoxCollider (ECS3DData), draw the
    // TODO:   "Render Collider" checkbox + gc::xyzGui Position/Rotation/Scale + "Scale All" drag.
    (void)component;
  });

  componentEditor.registerHandler("Sphere Collider", [](const std::shared_ptr<Component>& component) {
    // TODO: migrate SphereCollider::displayGui here: cast to SphereCollider (ECS3DData), draw the
    // TODO:   "Render Collider" checkbox, "Radius" drag, and gc::xyzGui Position.
    (void)component;
  });

  // TODO: the collider debug gizmo (BoxCollider/SphereCollider::variableUpdate, which drew the shape
  // TODO:   via a vke::RenderObject) belongs in ECS3DRender RenderSystem, gated on getRenderCollider().
}
