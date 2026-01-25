#ifndef TRANSFORM_H
#define TRANSFORM_H

#include "Component.h"
#include <glm/vec3.hpp>

class Transform final : public Component {
public:
  explicit Transform(const glm::vec3& position, const glm::vec3& scale, const glm::vec3& rotation);
  ~Transform() override = default;

  [[nodiscard]] uint8_t getUpdateID() const;

  [[nodiscard]] glm::vec3 getPosition() const;
  [[nodiscard]] glm::vec3 getScale() const;
  [[nodiscard]] glm::vec3 getRotation() const;

  void setScale(glm::vec3 scale);
  void setRotation(glm::vec3 rotation);

  void move(const glm::vec3& direction);

  void displayGui() override;

  [[nodiscard]] nlohmann::json serialize() override;

private:
  uint8_t m_updateID = 1;

  ComponentVariable<glm::vec3> m_position;
  ComponentVariable<glm::vec3> m_scale;
  ComponentVariable<glm::vec3> m_rotation;
};



#endif //TRANSFORM_H
