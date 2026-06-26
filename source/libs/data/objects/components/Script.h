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

  nlohmann::json m_fields = nlohmann::json::array();
};



#endif //SCRIPT_H
