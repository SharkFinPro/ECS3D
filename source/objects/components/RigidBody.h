#ifndef RIGIDBODY_H
#define RIGIDBODY_H

#include "Component.h"
#include <glm/vec3.hpp>
#include <memory>

class Transform;

class RigidBody final : public Component {
public:
  RigidBody();

  void fixedUpdate(float dt) override;

  void applyForce(const glm::vec3& force);

private:
  glm::vec3 velocity;

  bool doGravity;
  glm::vec3 gravity;

  std::weak_ptr<Transform> transform_ptr;
};



#endif //RIGIDBODY_H
