#include "ColliderEditor.h"
#include "../ComponentEditor.h"
#include <objects/components/collisions/BoxCollider.h>
#include <objects/components/collisions/SphereCollider.h>
#include <imgui.h>
#include <memory>

void registerColliderEditors(ComponentEditor& componentEditor)
{
  // Keyed by display name (componentTypeToString), since both colliders share a component type and
  // differ only by subType.
  componentEditor.registerHandler("Box Collider", [](const std::shared_ptr<Component>& component) -> bool {
    const auto box = std::dynamic_pointer_cast<BoxCollider>(component);
    if (!box)
    {
      return false;
    }

    bool edited = false;

    if (ComponentEditor::displayHeader(component))
    {
      bool renderCollider = box->getRenderCollider();
      if (ImGui::Checkbox("Render Collider", &renderCollider))
      {
        box->setRenderCollider(renderCollider);
        edited = true;
      }

      // TODO: gc::xyzGui Position/Rotation/Scale + "Scale All" — needs local position/scale/rotation
      // TODO:   setters added to BoxCollider (currently only the render flag is editable).
    }

    return edited;
  });

  componentEditor.registerHandler("Sphere Collider", [](const std::shared_ptr<Component>& component) -> bool {
    const auto sphere = std::dynamic_pointer_cast<SphereCollider>(component);
    if (!sphere)
    {
      return false;
    }

    bool edited = false;

    if (ComponentEditor::displayHeader(component))
    {
      bool renderCollider = sphere->getRenderCollider();
      if (ImGui::Checkbox("Render Collider", &renderCollider))
      {
        sphere->setRenderCollider(renderCollider);
        edited = true;
      }

      // TODO: "Radius" drag + gc::xyzGui Position — needs local radius/position setters on
      // TODO:   SphereCollider (currently only the render flag is editable).
    }

    return edited;
  });

  // TODO: the collider debug gizmo (the old BoxCollider/SphereCollider::variableUpdate that drew the
  // TODO:   shape via a vke::RenderObject) belongs in ECS3DRender RenderSystem, gated on getRenderCollider().
}
