#ifndef ECS3D_SCRIPT_H
#define ECS3D_SCRIPT_H

#include "Component.h"

class ScriptManager;

class Script final : public Component {
public:
  explicit Script(std::string className);

  void start() override;

  void stop() override;

  void variableUpdate() override;

  void fixedUpdate(float dt) override;

  void displayGui() override;

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

private:
  std::shared_ptr<ScriptManager> m_scriptManager;

  std::string m_className;
};


#endif //ECS3D_SCRIPT_H