#ifndef SPHERECOLLIDER_H
#define SPHERECOLLIDER_H

#include "Collider.h"
#include <glm/vec3.hpp>
#include <VulkanEngine/components/assets/objects/RenderObject.h>

class SphereCollider final : public Collider {
public:
  SphereCollider();

  float getRadius();

  void displayGui() override;

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  void variableUpdate(float dt) override;

private:
  std::shared_ptr<vke::RenderObject> m_renderObject = nullptr;

  bool m_renderCollider = false;

  ComponentVariable<float> m_radius = ComponentVariable(1.0f);

  glm::vec3 findFurthestPoint(const glm::vec3& direction) override;
};



#endif //SPHERECOLLIDER_H
