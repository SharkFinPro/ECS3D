#include "LightRendererEditor.h"
#include "../ComponentEditor.h"
#include "../GuiComponents.h"
#include "../MixedFields.h"
#include <objects/components/LightRenderer.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <memory>

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
      bool isSpotLight = light->isSpotLight();
      glm::vec3 color = light->getColor();
      float ambient = light->getAmbient();
      float diffuse = light->getDiffuse();
      float specular = light->getSpecular();
      glm::vec3 direction = light->getDirection();
      float coneAngle = light->getConeAngle();

      // These write the plain light data directly; the vke::PointLight/SpotLight are built by the
      // RenderSystem from these values each frame.
      if (gc::accentCheckbox("Spot Light", &isSpotLight, mixed.contains("isSpotlight")))
      {
        light->setSpotLight(isSpotLight);
        edited = true;
      }

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

      if (gc::accentSlider("Ambient", &ambient, 0.0f, 1.0f, mixed.contains("ambient")))
      {
        light->setAmbient(ambient);
        edited = true;
      }

      if (gc::accentSlider("Diffuse", &diffuse, 0.0f, 1.0f, mixed.contains("diffuse")))
      {
        light->setDiffuse(diffuse);
        edited = true;
      }

      if (gc::accentSlider("Specular", &specular, 0.0f, 1.0f, mixed.contains("specular")))
      {
        light->setSpecular(specular);
        edited = true;
      }

      if (gc::xyzGuiBoxed("Direction", &direction.x, &direction.y, &direction.z, 0.01f,
                          mixed.contains("direction")))
      {
        light->setDirection(direction);
        edited = true;
      }

      if (gc::accentSlider("Cone Angle", &coneAngle, LightRenderer::minConeAngleDegrees,
                           LightRenderer::maxConeAngleDegrees, mixed.contains("coneAngle")))
      {
        light->setConeAngle(coneAngle);
        edited = true;
      }
    }

    return edited;
  });
}
