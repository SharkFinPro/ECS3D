#include "LightRendererEditor.h"
#include "../ComponentEditor.h"
#include "../GuiComponents.h"
#include "../MixedFields.h"
#include <objects/components/LightRenderer.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <memory>

namespace {
  const char* lightTypeLabel(const LightType type)
  {
    switch (type)
    {
      case LightType::spot:
        return "Spot";
      case LightType::directional:
        return "Directional";
      case LightType::point:
        break;
    }

    return "Point";
  }
}

void registerLightRendererEditor(ComponentEditor& componentEditor)
{
  componentEditor.registerHandler("Light Renderer",
    [](const std::shared_ptr<Component>& component, const MixedFields& mixed) -> bool {
    const auto light = std::dynamic_pointer_cast<LightRenderer>(component);
    if (!light)
    {
      return false;
    }

    bool edited = false;

    if (ComponentEditor::displayHeader(component))
    {
      const LightType type = light->getLightType();
      glm::vec3 color = light->getColor();
      float ambient = light->getAmbient();
      float diffuse = light->getDiffuse();
      float specular = light->getSpecular();
      glm::vec3 direction = light->getDirection();
      float coneAngle = light->getConeAngle();
      float range = light->getRange();
      float shadowDistance = light->getShadowDistance();

      // These write the plain light data directly; the vke lights are built by the RenderSystem from
      // these values each frame.
      gc::rowLabel("Type");
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
      ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed.contains("lightType"));
      const bool typeOpen = ImGui::BeginCombo("##LightType", lightTypeLabel(type));
      ImGui::PopItemFlag();
      if (typeOpen)
      {
        for (const LightType choice : { LightType::point, LightType::spot, LightType::directional })
        {
          const bool picked = ImGui::Selectable(lightTypeLabel(choice), choice == type && !mixed.contains("lightType"));
          if (picked && choice != light->getLightType())
          {
            light->setLightType(choice);
            edited = true;
          }
        }

        ImGui::EndCombo();
      }

      // Rows follow the type shown here, so a mixed selection lays out for its primary object.
      const LightType shownType = light->getLightType();
      const bool isSpot = shownType == LightType::spot;
      const bool isDirectional = shownType == LightType::directional;

      gc::rowLabel("Color");
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
      ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed.contains("color"));
      const bool colorEdited = ImGui::ColorEdit3("##Color", value_ptr(color));
      ImGui::PopItemFlag();
      if (colorEdited)
      {
        light->setColor(color);
        edited = true;
      }

      // A ranged light is attenuated hard, so it needs headroom well past 1 to light anything a few
      // units away.
      const float intensityMax = !isDirectional && range > 0.0f ? 100.0f : 1.0f;

      if (gc::accentSlider("Ambient", &ambient, 0.0f, 1.0f, mixed.contains("ambient")))
      {
        light->setAmbient(ambient);
        edited = true;
      }

      if (gc::accentSlider("Diffuse", &diffuse, 0.0f, intensityMax, mixed.contains("diffuse")))
      {
        light->setDiffuse(diffuse);
        edited = true;
      }

      if (gc::accentSlider("Specular", &specular, 0.0f, intensityMax, mixed.contains("specular")))
      {
        light->setSpecular(specular);
        edited = true;
      }

      if (!isDirectional && gc::accentSlider("Range", &range, 0.0f, 100.0f, mixed.contains("range")))
      {
        light->setRange(range);
        edited = true;
      }

      if ((isSpot || isDirectional) &&
          gc::xyzGuiBoxed("Direction", &direction.x, &direction.y, &direction.z, 0.01f,
                          mixed.contains("direction")))
      {
        light->setDirection(direction);
        edited = true;
      }

      if (isSpot && gc::accentSlider("Cone Angle", &coneAngle, LightRenderer::minConeAngleDegrees,
                                     LightRenderer::maxConeAngleDegrees, mixed.contains("coneAngle")))
      {
        light->setConeAngle(coneAngle);
        edited = true;
      }

      if (isDirectional && gc::accentSlider("Shadow Distance", &shadowDistance, LightRenderer::minShadowDistance,
                                            LightRenderer::maxShadowDistance, mixed.contains("shadowDistance")))
      {
        light->setShadowDistance(shadowDistance);
        edited = true;
      }
    }

    return edited;
  });
}
