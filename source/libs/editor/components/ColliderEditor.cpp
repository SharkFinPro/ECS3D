#include "ColliderEditor.h"
#include "../ComponentEditor.h"
#include "../GuiComponents.h"
#include <objects/components/collisions/BoxCollider.h>
#include <objects/components/collisions/SphereCollider.h>
#include <glm/vec3.hpp>
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

      glm::vec3 position = box->getLocalPosition();
      glm::vec3 rotation = box->getLocalRotation();
      glm::vec3 scale = box->getLocalScale();

      ImGui::PushID("BoxColliderPosition");
      if (gc::xyzGui("Position", &position.x, &position.y, &position.z))
      {
        box->setPosition(position);
        edited = true;
      }
      ImGui::PopID();

      ImGui::PushID("BoxColliderRotation");
      if (gc::xyzGui("Rotation", &rotation.x, &rotation.y, &rotation.z))
      {
        box->setRotation(rotation);
        edited = true;
      }
      ImGui::PopID();

      ImGui::PushID("BoxColliderScale");
      if (gc::xyzGui("Scale", &scale.x, &scale.y, &scale.z))
      {
        box->setScale(scale);
        edited = true;
      }
      ImGui::PopID();
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

      float radius = sphere->getLocalRadius();
      if (ImGui::DragFloat("Radius", &radius, 0.1f, 0.0f, 0.0f))
      {
        sphere->setRadius(radius);
        edited = true;
      }

      glm::vec3 position = sphere->getLocalPosition();
      ImGui::PushID("SphereColliderPosition");
      if (gc::xyzGui("Position", &position.x, &position.y, &position.z))
      {
        sphere->setPosition(position);
        edited = true;
      }
      ImGui::PopID();
    }

    return edited;
  });

  // TODO: the collider debug gizmo (the old BoxCollider/SphereCollider::variableUpdate that drew the
  // TODO:   shape via a vke::RenderObject) belongs in ECS3DRender RenderSystem, gated on getRenderCollider().
}
