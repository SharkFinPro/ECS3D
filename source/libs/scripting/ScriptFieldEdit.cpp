#include "ScriptFieldEdit.h"

#include <nlohmann/json.hpp>

namespace {
  bool isVector3Value(const nlohmann::json& value)
  {
    if (!value.is_array() || value.size() != 3)
    {
      return false;
    }

    for (const auto& element : value)
    {
      if (!element.is_number())
      {
        return false;
      }
    }

    return true;
  }
}

namespace scripting {
  std::string rejectFieldEdit(const nlohmann::json& field, const std::string* cachedType)
  {
    if (!field.contains("name") || !field.contains("type") || !field.contains("value")
        || !field.at("name").is_string() || !field.at("type").is_string())
    {
      return "field entry is missing name, type or value";
    }

    const auto name = field.at("name").get<std::string>();
    const auto tag = field.at("type").get<std::string>();

    if (!cachedType)
    {
      return "field '" + name + "' is not exposed by the script";
    }

    if (tag != *cachedType)
    {
      return "field '" + name + "' is " + *cachedType + " but the edit says " + tag;
    }

    const auto& value = field.at("value");

    bool matches;
    if (tag == "float")
    {
      matches = value.is_number();
    }
    else if (tag == "int")
    {
      matches = value.is_number_integer();
    }
    else if (tag == "bool")
    {
      matches = value.is_boolean();
    }
    else if (tag == "vector3")
    {
      matches = isVector3Value(value);
    }
    else
    {
      return "field '" + name + "' has unsupported type " + tag;
    }

    if (!matches)
    {
      return "field '" + name + "' value is not a " + tag;
    }

    return {};
  }
}
