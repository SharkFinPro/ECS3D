#ifndef PLAYER_H
#define PLAYER_H

#include "Component.h"
#include <glm/vec3.hpp>
#include <memory>

class Transform;
class RigidBody;

class Player final : public Component {
public:
  Player();

  void variableUpdate(float dt) override;

  void fixedUpdate(float dt) override;

private:
  float speed;
  float jumpHeight;

  glm::vec3 appliedForce;

  std::weak_ptr<Transform> transform_ptr;
  std::weak_ptr<RigidBody> rigidBody_ptr;

  void handleInput();
};



#endif //PLAYER_H
