#include "ColliderEditor.h"
#include "AssetSlot.h"
#include "ConvexHullFromModel.h"
#include "../AssetDragDrop.h"
#include "../ComponentEditor.h"
#include "../GuiComponents.h"
#include "../MixedFields.h"
#include <objects/components/collisions/Collider.h>
#include <objects/components/collisions/BoxCollider.h>
#include <objects/components/collisions/CapsuleCollider.h>
#include <objects/components/collisions/ConvexMeshCollider.h>
#include <objects/components/collisions/SphereCollider.h>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>

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

  // Layer index (0-31) + collision mask rows, shared by every collider shape since the fields live on the
  // Collider base. The mask is edited as per-layer checkboxes in a popup rather than a raw bitmask.
  // Returns true if either was edited this frame.
  bool colliderLayerMaskEditor(const std::shared_ptr<Collider>& collider, const MixedFields& mixed)
  {
    bool edited = false;

    // This collider's own layer: a drag field flanked by -/+ steppers.
    int layer = static_cast<int>(collider->getLayer());
    bool layerEdited = false;
    const bool layerMixed = mixed.contains("layer");

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
    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, layerMixed);
    if (ImGui::DragInt("##layer", &layer, 0.1f, 0, 31))
    {
      layerEdited = true;
    }
    ImGui::PopItemFlag();
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

    // Which layers this collider collides with, as a checklist popup (bit N = layer N). The button is
    // custom-drawn text rather than a stock widget ImGuiItemFlags_MixedValue can format, so a disagreeing
    // mask shows "Mixed" on the button face directly instead.
    uint32_t mask = collider->getMask();
    bool maskEdited = false;
    const bool maskMixed = mixed.contains("mask");

    gc::rowLabel("Collides With");
    ImGui::PushID("ColliderMask");
    if (ImGui::Button(maskMixed ? "Mixed" : maskSummary(mask).c_str(),
                      ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
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

  // "Render Collider" checkbox, shared by every collider shape since the flag lives on the Collider base.
  // Returns true if it was edited this frame.
  bool colliderRenderGizmoEditor(const std::shared_ptr<Collider>& collider, const MixedFields& mixed)
  {
    bool renderCollider = collider->getRenderCollider();
    if (gc::accentCheckbox("Render Collider", &renderCollider, mixed.contains("renderCollider")))
    {
      collider->setRenderCollider(renderCollider);
      return true;
    }

    return false;
  }

  // The capsule's own fields; radius and height clamp to zero in their setters, so a drag below it holds there.
  bool capsuleShapeEditor(const std::shared_ptr<CapsuleCollider>& capsule, const MixedFields& mixed)
  {
    bool edited = false;

    glm::vec3 position = capsule->getLocalPosition();
    glm::vec3 rotation = capsule->getLocalRotation();

    ImGui::PushID("CapsuleCollider");
    if (gc::xyzGuiBoxed("Position", &position.x, &position.y, &position.z, 0.1f, mixed.contains("position")))
    {
      capsule->setPosition(position);
      edited = true;
    }

    if (gc::xyzGuiBoxed("Rotation", &rotation.x, &rotation.y, &rotation.z, 0.1f, mixed.contains("rotation")))
    {
      capsule->setRotation(rotation);
      edited = true;
    }
    ImGui::PopID();

    float radius = capsule->getLocalRadius();
    if (gc::labeledDrag("Radius", &radius, 0.1f, mixed.contains("radius")))
    {
      capsule->setRadius(radius);
      edited = true;
    }

    float height = capsule->getLocalHeight();
    if (gc::labeledDrag("Height", &height, 0.1f, mixed.contains("height")))
    {
      capsule->setHeight(height);
      edited = true;
    }

    return edited;
  }

  bool capsuleColliderEditor(const std::shared_ptr<Component>& component, const MixedFields& mixed)
  {
    const auto capsule = std::dynamic_pointer_cast<CapsuleCollider>(component);
    if (!capsule)
    {
      return false;
    }

    bool edited = false;

    if (ComponentEditor::displayHeader(component))
    {
      edited |= colliderRenderGizmoEditor(capsule, mixed);

      bool isTrigger = capsule->isTrigger();
      if (gc::accentCheckbox("Is Trigger", &isTrigger, mixed.contains("isTrigger")))
      {
        capsule->setIsTrigger(isTrigger);
        edited = true;
      }

      edited |= colliderLayerMaskEditor(capsule, mixed);
      edited |= capsuleShapeEditor(capsule, mixed);
    }

    return edited;
  }

  // The source model slot, the hull vertex count and the Rebuild button. Picking a model builds its hull at
  // once; Rebuild redoes it for the model already set. True when the hull changed.
  bool convexMeshHullEditor(ConvexMeshCollider& collider, const std::shared_ptr<GpuAssetCache>& cache,
                            const AssetRegistry* registry, const MixedFields& mixed)
  {
    bool edited = false;

    const bool hullMixed = mixed.contains("vertices") || mixed.contains("indices") || mixed.contains("sourceModel");

    uuids::uuid dropped;
    const AssetSlotStyle slot{ "convexModelSlot", "Source Model", assetDragDrop::model, false,
                               gc::SecIcon::model, theme::modelPurple };
    // A drop on a mixed selection sets every selected hull to the dropped model on purpose, even when the
    // primary already holds it, so it reports an edit then.
    if (drawAssetSlot(cache, registry, slot, collider.getSourceModel(), dropped, hullMixed))
    {
      edited |= rebuildHullFromModel(collider, cache.get(), dropped, hullMixed);
    }

    if (hullMixed)
    {
      ImGui::TextDisabled("Hull vertices: Mixed");
    }
    else
    {
      ImGui::TextDisabled("Hull vertices: %zu", collider.getHull().vertices.size());
    }

    // Rebuild would copy the primary object's hull onto every selected object, so it needs agreement.
    ImGui::BeginDisabled(hullMixed || collider.getSourceModel().is_nil() || !cache);
    if (ImGui::Button("Rebuild hull"))
    {
      edited |= rebuildHullFromModel(collider, cache.get(), collider.getSourceModel());
    }
    ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
      ImGui::SetTooltip("Rebuild the hull from the source model. Only the model's first mesh is used.");
    }

    return edited;
  }

  bool convexMeshOffsetEditor(ConvexMeshCollider& collider, const MixedFields& mixed)
  {
    bool edited = false;

    glm::vec3 position = collider.getLocalPosition();
    glm::vec3 rotation = collider.getLocalRotation();
    glm::vec3 scale = collider.getLocalScale();

    ImGui::PushID("ConvexMeshCollider");
    if (gc::xyzGuiBoxed("Position", &position.x, &position.y, &position.z, 0.1f, mixed.contains("position")))
    {
      collider.setPosition(position);
      edited = true;
    }

    if (gc::xyzGuiBoxed("Rotation", &rotation.x, &rotation.y, &rotation.z, 0.1f, mixed.contains("rotation")))
    {
      collider.setRotation(rotation);
      edited = true;
    }

    if (gc::xyzGuiBoxed("Scale", &scale.x, &scale.y, &scale.z, 0.1f, mixed.contains("scale")))
    {
      collider.setScale(scale);
      edited = true;
    }
    ImGui::PopID();

    return edited;
  }

  void registerConvexMeshEditor(ComponentEditor& componentEditor, std::shared_ptr<GpuAssetCache> assetCache,
                                const AssetRegistry* assetRegistry)
  {
    componentEditor.registerHandler("Convex Mesh Collider",
      [cache = std::move(assetCache), registry = assetRegistry](const std::shared_ptr<Component>& component,
                                                                 const MixedFields& mixed) -> bool {
      const auto hull = std::dynamic_pointer_cast<ConvexMeshCollider>(component);
      if (!hull)
      {
        return false;
      }

      bool edited = false;

      if (ComponentEditor::displayHeader(component))
      {
        edited |= colliderRenderGizmoEditor(hull, mixed);

        bool isTrigger = hull->isTrigger();
        if (gc::accentCheckbox("Is Trigger", &isTrigger, mixed.contains("isTrigger")))
        {
          hull->setIsTrigger(isTrigger);
          edited = true;
        }

        edited |= colliderLayerMaskEditor(hull, mixed);
        edited |= convexMeshOffsetEditor(*hull, mixed);

        ImGui::Spacing();
        edited |= convexMeshHullEditor(*hull, cache, registry, mixed);
      }

      return edited;
    });
  }
}

void registerColliderEditors(ComponentEditor& componentEditor, std::shared_ptr<GpuAssetCache> assetCache,
                             const AssetRegistry* assetRegistry)
{
  registerConvexMeshEditor(componentEditor, std::move(assetCache), assetRegistry);

  // Keyed by display name (componentTypeToString), since both colliders share a component type and
  // differ only by subType.
  componentEditor.registerHandler("Box Collider",
    [](const std::shared_ptr<Component>& component, const MixedFields& mixed) -> bool {
    const auto box = std::dynamic_pointer_cast<BoxCollider>(component);
    if (!box)
    {
      return false;
    }

    bool edited = false;

    if (ComponentEditor::displayHeader(component))
    {
      edited |= colliderRenderGizmoEditor(box, mixed);

      bool isTrigger = box->isTrigger();
      if (gc::accentCheckbox("Is Trigger", &isTrigger, mixed.contains("isTrigger")))
      {
        box->setIsTrigger(isTrigger);
        edited = true;
      }

      edited |= colliderLayerMaskEditor(box, mixed);

      glm::vec3 position = box->getLocalPosition();
      glm::vec3 rotation = box->getLocalRotation();
      glm::vec3 scale = box->getLocalScale();

      ImGui::PushID("BoxCollider");
      if (gc::xyzGuiBoxed("Position", &position.x, &position.y, &position.z, 0.1f, mixed.contains("position")))
      {
        box->setPosition(position);
        edited = true;
      }

      if (gc::xyzGuiBoxed("Rotation", &rotation.x, &rotation.y, &rotation.z, 0.1f, mixed.contains("rotation")))
      {
        box->setRotation(rotation);
        edited = true;
      }

      if (gc::xyzGuiBoxed("Scale", &scale.x, &scale.y, &scale.z, 0.1f, mixed.contains("scale")))
      {
        box->setScale(scale);
        edited = true;
      }
      ImGui::PopID();
    }

    return edited;
  });

  componentEditor.registerHandler("Sphere Collider",
    [](const std::shared_ptr<Component>& component, const MixedFields& mixed) -> bool {
    const auto sphere = std::dynamic_pointer_cast<SphereCollider>(component);
    if (!sphere)
    {
      return false;
    }

    bool edited = false;

    if (ComponentEditor::displayHeader(component))
    {
      edited |= colliderRenderGizmoEditor(sphere, mixed);

      bool isTrigger = sphere->isTrigger();
      if (gc::accentCheckbox("Is Trigger", &isTrigger, mixed.contains("isTrigger")))
      {
        sphere->setIsTrigger(isTrigger);
        edited = true;
      }

      edited |= colliderLayerMaskEditor(sphere, mixed);

      float radius = sphere->getLocalRadius();
      if (gc::labeledDrag("Radius", &radius, 0.1f, mixed.contains("radius")))
      {
        sphere->setRadius(radius);
        edited = true;
      }

      glm::vec3 position = sphere->getLocalPosition();
      ImGui::PushID("SphereCollider");
      if (gc::xyzGuiBoxed("Position", &position.x, &position.y, &position.z, 0.1f, mixed.contains("position")))
      {
        sphere->setPosition(position);
        edited = true;
      }
      ImGui::PopID();
    }

    return edited;
  });

  componentEditor.registerHandler("Capsule Collider", capsuleColliderEditor);

  // The collider debug gizmo (the shape drawn as an outline when "Render Collider"
  // is on) is handled by ECS3DRender's RenderSystem via GpuAssetCache::getColliderGizmo.
}
