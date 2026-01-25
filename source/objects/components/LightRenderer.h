#ifndef LIGHTRENDERER_H
#define LIGHTRENDERER_H

#include "Component.h"
#include <glm/glm.hpp>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/lighting/lights/PointLight.h>
#include <VulkanEngine/components/lighting/lights/SpotLight.h>
#include <memory>

class Transform;

class LightRenderer final : public Component {
public:
  LightRenderer(const std::shared_ptr<vke::VulkanEngine>& renderer,
                glm::vec3 color,
                float ambient,
                float diffuse,
                float specular);

  ~LightRenderer() override = default;

  void variableUpdate(float dt) override;

  void displayGui() override;

  [[nodiscard]] nlohmann::json serialize() override;

private:
  bool m_isSpotLight = false;

  std::shared_ptr<vke::PointLight> m_pointLight;
  std::shared_ptr<vke::SpotLight> m_spotLight;
  std::weak_ptr<Transform> m_transform_ptr;
};



#endif //LIGHTRENDERER_H
