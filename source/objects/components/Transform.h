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

  void start() override;

  void stop() override;

private:
  uint8_t updateID;

  glm::vec3 initialPosition;
  glm::vec3 livePosition;
  glm::vec3* currentPosition;

  glm::vec3 initialScale;
  glm::vec3 liveScale;
  glm::vec3* currentScale;

  glm::vec3 initialRotation;
  glm::vec3 liveRotation;
  glm::vec3* currentRotation;
};



#endif //TRANSFORM_H
