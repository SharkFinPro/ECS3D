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
  
  [[nodiscard]] glm::vec3 getPosition() override;

private:
  std::shared_ptr<vke::RenderObject> m_renderObject = nullptr;

  bool m_renderCollider = false;

  ComponentVariable<float> m_radius = ComponentVariable(1.0f);

  ComponentVariable<glm::vec3> m_position = ComponentVariable(glm::vec3(0));

  glm::vec3 findFurthestPoint(const glm::vec3& direction) override;

  void updateTransformPointer();
};



#endif //SPHERECOLLIDER_H
