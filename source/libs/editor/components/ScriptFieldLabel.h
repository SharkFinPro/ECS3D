#ifndef SCRIPTFIELDLABEL_H
#define SCRIPTFIELDLABEL_H

#include <nlohmann/json.hpp>
#include <string>

// The text a script field's widget shows: its displayName when it has a non-empty one, else its name.
[[nodiscard]] inline std::string scriptFieldLabel(const nlohmann::json& field)
{
  const auto display = field.find("displayName");
  if (display != field.end() && display->is_string() && !display->get<std::string>().empty())
  {
    return display->get<std::string>();
  }

  return field.at("name").get<std::string>();
}

#endif //SCRIPTFIELDLABEL_H
