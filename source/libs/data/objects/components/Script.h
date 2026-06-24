#ifndef SCRIPT_H
#define SCRIPT_H

#include "Component.h"
#include <nlohmann/json.hpp>
#include <string>

class Script final : public Component {
public:
  Script();

  explicit Script(std::string className);

  [[nodiscard]] std::string getClassName() const;
  void setClassName(const std::string& className);

  [[nodiscard]] const nlohmann::json& getFields() const;
  void setFields(const nlohmann::json& fields);

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

private:
  std::string m_className;

  // The exposed-field name/type/value blob. The live values live in the C# script instance on the
  // server; ECS3DScripting syncs this blob <-> the instance (reads it on snapshot, writes it on
  // load/start). The data class just carries it so it can be serialized/replicated.
  nlohmann::json m_fields = nlohmann::json::array();
};



#endif //SCRIPT_H
