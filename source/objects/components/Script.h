#ifndef ECS3D_SCRIPT_H
#define ECS3D_SCRIPT_H

#include "Component.h"
#include <nlohmann/json.hpp>

class ScriptManager;

class Script final : public Component {
public:
  Script(std::string className,
         std::shared_ptr<ScriptManager> scriptManager);

  ~Script() override;

  void variableUpdate() override;

  void fixedUpdate(float dt) override;

  void displayGui() override;

  void start() override;

  void stop() override;

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  void preReload();

  void postReload();

  [[nodiscard]] std::string getClassName() const;

private:
  std::shared_ptr<ScriptManager> m_scriptManager;

  std::string m_className;

  nlohmann::json m_tempData = nlohmann::json();

  void attachScript();

  void displayFieldsGui() const;
};


#endif //ECS3D_SCRIPT_H