#include "ColliderEditor.h"
#include "../ComponentEditor.h"
#include "../GuiComponents.h"
#include <objects/components/collisions/Collider.h>
#include <objects/components/collisions/BoxCollider.h>
#include <objects/components/collisions/SphereCollider.h>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

namespace {
  // A one-line summary of which layers a mask includes, for the "Collides With" button face.
  std::string maskSummary(const uint32_t mask)
  {
    if (mask == 0xFFFFFFFFu)
    {
      return "All layers";
    }
    if (mask == 0u)
    {
      return "Nothing";
    }

    std::string summary;
    int listed = 0;
    for (int i = 0; i < 32; ++i)
    {
      if ((mask & (1u << i)) == 0u)
      {
        continue;
      }

      if (listed == 6)
      {
        summary += ", ...";
        break;
      }

      if (!summary.empty())
      {
        summary += ", ";
      }
      summary += std::to_string(i);
      ++listed;
    }

    return summary;
  }

  // Layer index (0-31) + collision mask rows, shared by both collider shapes since the fields live on the
  // Collider base. The mask is edited as per-layer checkboxes in a popup rather than a raw bitmask.
  // Returns true if either was edited this frame.
  bool colliderLayerMaskEditor(const std::shared_ptr<Collider>& collider)
  {
    bool edited = false;

    // This collider's own layer: a drag field flanked by -/+ steppers.
    int layer = static_cast<int>(collider->getLayer());
    bool layerEdited = false;

    gc::rowLabel("Layer");
    ImGui::PushID("ColliderLayer");
    const float step = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float dragWidth = ImGui::GetContentRegionAvail().x - 2.0f * (step + spacing);

    if (ImGui::Button("-", ImVec2(step, step)) && layer > 0)
    {
      --layer;
      layerEdited = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(dragWidth);
    if (ImGui::DragInt("##layer", &layer, 0.1f, 0, 31))
    {
      layerEdited = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("+", ImVec2(step, step)) && layer < 31)
    {
      ++layer;
      layerEdited = true;
    }
    ImGui::PopID();

    if (layerEdited)
    {
      collider->setLayer(static_cast<uint32_t>(std::clamp(layer, 0, 31)));
      edited = true;
    }

    // Which layers this collider collides with, as a checklist popup (bit N = layer N).
    uint32_t mask = collider->getMask();
    bool maskEdited = false;

    gc::rowLabel("Collides With");
    ImGui::PushID("ColliderMask");
    if (ImGui::Button(maskSummary(mask).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
    {
      ImGui::OpenPopup("maskPopup");
    }

    if (ImGui::BeginPopup("maskPopup"))
    {
      if (ImGui::SmallButton("All"))
      {
        mask = 0xFFFFFFFFu;
        maskEdited = true;
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("None"))
      {
        mask = 0u;
        maskEdited = true;
      }

      ImGui::Separator();

      // 32 layers is tall; keep the list in a fixed, scrollable box.
      ImGui::BeginChild("maskLayers", ImVec2(150.0f, 200.0f), false);
      for (int i = 0; i < 32; ++i)
      {
        bool on = (mask & (1u << i)) != 0u;
        char label[16];
        std::snprintf(label, sizeof(label), "Layer %d", i);
        if (ImGui::Checkbox(label, &on))
        {
          mask = on ? (mask | (1u << i)) : (mask & ~(1u << i));
          maskEdited = true;
        }
      }
      ImGui::EndChild();

      ImGui::EndPopup();
    }
    ImGui::PopID();

    if (maskEdited)
    {
      collider->setMask(mask);
      edited = true;
    }

    return edited;
  }
}

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
      if (gc::accentCheckbox("Render Collider", &renderCollider))
      {
        box->setRenderCollider(renderCollider);
        edited = true;
      }

      bool isTrigger = box->isTrigger();
      if (gc::accentCheckbox("Is Trigger", &isTrigger))
      {
        box->setIsTrigger(isTrigger);
        edited = true;
      }

      edited |= colliderLayerMaskEditor(box);

      glm::vec3 position = box->getLocalPosition();
      glm::vec3 rotation = box->getLocalRotation();
      glm::vec3 scale = box->getLocalScale();

      ImGui::PushID("BoxCollider");
      if (gc::xyzGuiBoxed("Position", &position.x, &position.y, &position.z))
      {
        box->setPosition(position);
        edited = true;
      }

      if (gc::xyzGuiBoxed("Rotation", &rotation.x, &rotation.y, &rotation.z))
      {
        box->setRotation(rotation);
        edited = true;
      }

      if (gc::xyzGuiBoxed("Scale", &scale.x, &scale.y, &scale.z))
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
      if (gc::accentCheckbox("Render Collider", &renderCollider))
      {
        sphere->setRenderCollider(renderCollider);
        edited = true;
      }

      bool isTrigger = sphere->isTrigger();
      if (gc::accentCheckbox("Is Trigger", &isTrigger))
      {
        sphere->setIsTrigger(isTrigger);
        edited = true;
      }

      edited |= colliderLayerMaskEditor(sphere);

      float radius = sphere->getLocalRadius();
      if (gc::labeledDrag("Radius", &radius))
      {
        sphere->setRadius(radius);
        edited = true;
      }

      glm::vec3 position = sphere->getLocalPosition();
      ImGui::PushID("SphereCollider");
      if (gc::xyzGuiBoxed("Position", &position.x, &position.y, &position.z))
      {
        sphere->setPosition(position);
        edited = true;
      }
      ImGui::PopID();
    }

    return edited;
  });

  // The collider debug gizmo (the shape drawn with the objectHighlight pipeline when "Render Collider"
  // is on) is handled by ECS3DRender's RenderSystem via GpuAssetCache::getColliderGizmo.
}
