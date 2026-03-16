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

  void displayGui() override;

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

private:
  ComponentVariable<float> m_speed{1.0f};
  ComponentVariable<float> m_jumpHeight{15.0f};
};



#endif //PLAYER_H
