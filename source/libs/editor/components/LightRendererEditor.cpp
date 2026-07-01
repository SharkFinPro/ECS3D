#include "LightRendererEditor.h"
#include "../ComponentEditor.h"
#include "../GuiComponents.h"
#include <objects/components/LightRenderer.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <memory>

void registerLightRendererEditor(ComponentEditor& componentEditor)
{
  componentEditor.registerHandler("Light Renderer", [](const std::shared_ptr<Component>& component) -> bool {
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

      // These now write the plain light data directly (the vke::PointLight/SpotLight are built by the
      // RenderSystem from these values each frame).
      if (gc::accentCheckbox("Spot Light", &isSpotLight))
      {
        light->setSpotLight(isSpotLight);
        edited = true;
      }

      gc::rowLabel("Color");
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
      if (ImGui::ColorEdit3("##Color", value_ptr(color)))
      {
        light->setColor(color);
        edited = true;
      }

      if (gc::accentSlider("Ambient", &ambient, 0.0f, 1.0f))
      {
        light->setAmbient(ambient);
        edited = true;
      }

      if (gc::accentSlider("Diffuse", &diffuse, 0.0f, 1.0f))
      {
        light->setDiffuse(diffuse);
        edited = true;
      }

      if (gc::accentSlider("Specular", &specular, 0.0f, 1.0f))
      {
        light->setSpecular(specular);
        edited = true;
      }

      if (gc::xyzGuiBoxed("Direction", &direction.x, &direction.y, &direction.z, 0.01f))
      {
        light->setDirection(direction);
        edited = true;
      }

      if (gc::accentSlider("Cone Angle", &coneAngle, 0.0f, 180.0f))
      {
        light->setConeAngle(coneAngle);
        edited = true;
      }
    }

    return edited;
  });
}
