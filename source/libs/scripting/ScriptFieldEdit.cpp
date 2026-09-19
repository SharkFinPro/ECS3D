#include "ScriptFieldEdit.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>

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

  // A json integer is wider than int, and the native setter's get<int>() would narrow it silently.
  bool fitsInInt(const nlohmann::json& value)
  {
    if (value.is_number_unsigned())
    {
      return value.get<std::uint64_t>() <= static_cast<std::uint64_t>(std::numeric_limits<int>::max());
    }

    const auto wide = value.get<std::int64_t>();

    return wide >= static_cast<std::int64_t>(std::numeric_limits<int>::min())
        && wide <= static_cast<std::int64_t>(std::numeric_limits<int>::max());
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
      if (value.is_number_integer() && !fitsInInt(value))
      {
        return "field '" + name + "' value is out of range for int";
      }

      matches = value.is_number_integer();
    }
    else if (tag == "string")
    {
      matches = value.is_string();
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
      return "field '" + name + "' value does not match type " + tag;
    }

    return {};
  }
}
