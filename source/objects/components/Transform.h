#ifndef TRANSFORM_H
#define TRANSFORM_H

#include "Component.h"
#include <glm/vec3.hpp>

class Transform final : public Component {
public:
  explicit Transform(const glm::vec3& position, const glm::vec3& scale, const glm::vec3& rotation);
  ~Transform() override = default;

  [[nodiscard]] glm::vec3 getPosition() const;
  [[nodiscard]] glm::vec3 getScale() const;
  [[nodiscard]] glm::vec3 getRotation() const;

  void setScale(glm::vec3 scale);
  void setRotation(glm::vec3 rotation);

  void move(const glm::vec3& direction);

  void displayGui() override;

  void reset() override;

private:
  glm::vec3 initialPosition;
  glm::vec3 position;

  glm::vec3 initialScale;
  glm::vec3 scale;

  glm::vec3 initialRotation;
  glm::vec3 rotation;
};



#endif //TRANSFORM_H
