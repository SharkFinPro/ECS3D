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

  void displayGui() override;

private:
  float initialSpeed;
  float liveSpeed;
  float* currentSpeed;

  float initialJumpHeight;
  float liveJumpHeight;
  float* currentJumpHeight;

  glm::vec3 initialAppliedForce;
  glm::vec3 liveAppliedForce;
  glm::vec3* currentAppliedForce;

  std::weak_ptr<Transform> transform_ptr;
  std::weak_ptr<RigidBody> rigidBody_ptr;

  void handleInput();
};



#endif //PLAYER_H
