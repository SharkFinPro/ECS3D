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

  [[nodiscard]] nlohmann::json serialize() override;

private:
  ComponentVariable<float> m_speed{1.0f};
  ComponentVariable<float> m_jumpHeight{15.0f};

  glm::vec3 m_appliedForce = glm::vec3(0.0f);

  std::weak_ptr<Transform> m_transform_ptr;
  std::weak_ptr<RigidBody> m_rigidBody_ptr;

  void handleInput();
};



#endif //PLAYER_H
