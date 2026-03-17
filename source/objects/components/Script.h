#ifndef ECS3D_SCRIPT_H
#define ECS3D_SCRIPT_H

#include "Component.h"
#include <nlohmann/json.hpp>

class ScriptManager;

class Script final : public Component {
public:
  explicit Script(std::string className);

  ~Script() override;

  void variableUpdate() override;

  void fixedUpdate(float dt) override;

  void displayGui() override;

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  void preReload();

  void postReload();

private:
  std::shared_ptr<ScriptManager> m_scriptManager;

  std::string m_className;

  nlohmann::json m_tempData = nlohmann::json();

  void attachScript();
};


#endif //ECS3D_SCRIPT_H