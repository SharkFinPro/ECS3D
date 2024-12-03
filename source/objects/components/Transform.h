#ifndef TRANSFORM_H
#define TRANSFORM_H

#include "Component.h"
#include <glm/vec3.hpp>

class Transform final : public Component {
public:
  explicit Transform(const glm::vec3& position);
  ~Transform() override = default;

  [[nodiscard]] glm::vec3 getPosition() const;

  void move(const glm::vec3& direction);

  void reset();

private:
  glm::vec3 initialPosition;
  glm::vec3 position;
};



#endif //TRANSFORM_H
