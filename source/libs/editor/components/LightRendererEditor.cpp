#include "LightRendererEditor.h"
#include "../ComponentEditor.h"
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
      if (ImGui::Checkbox("Spot Light", &isSpotLight))
      {
        light->setSpotLight(isSpotLight);
        edited = true;
      }

      if (ImGui::ColorEdit3("Color", value_ptr(color)))
      {
        light->setColor(color);
        edited = true;
      }

      if (ImGui::SliderFloat("Ambient", &ambient, 0.0f, 1.0f))
      {
        light->setAmbient(ambient);
        edited = true;
      }

      if (ImGui::SliderFloat("Diffuse", &diffuse, 0.0f, 1.0f))
      {
        light->setDiffuse(diffuse);
        edited = true;
      }

      if (ImGui::SliderFloat("Specular", &specular, 0.0f, 1.0f))
      {
        light->setSpecular(specular);
        edited = true;
      }

      if (ImGui::SliderFloat3("Direction", value_ptr(direction), -1.0f, 1.0f))
      {
        light->setDirection(direction);
        edited = true;
      }

      if (ImGui::SliderFloat("Cone Angle", &coneAngle, 0.0f, 180.0f))
      {
        light->setConeAngle(coneAngle);
        edited = true;
      }
    }

    return edited;
  });
}
