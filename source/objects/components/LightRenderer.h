#ifndef LIGHTRENDERER_H
#define LIGHTRENDERER_H

#include "Component.h"
#include <glm/glm.hpp>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/lighting/lights/PointLight.h>
#include <memory>

class Transform;

class LightRenderer final : public Component {
public:
  LightRenderer(std::shared_ptr<vke::VulkanEngine> renderer, glm::vec3 position, glm::vec3 color, float ambient, float diffuse, float specular);

  ~LightRenderer() override = default;

  void variableUpdate(float dt) override;

  void displayGui() override;

private:
  std::shared_ptr<vke::PointLight> light;
  std::weak_ptr<Transform> transform_ptr;
};



#endif //LIGHTRENDERER_H
