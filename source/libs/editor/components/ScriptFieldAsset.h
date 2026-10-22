#ifndef SCRIPTFIELDASSET_H
#define SCRIPTFIELDASSET_H

#include <nlohmann/json.hpp>
#include <uuid.h>
#include <string>

// Whether a script field is a string holding a prefab's uuid, so it is drawn as a prefab slot.
[[nodiscard]] inline bool isPrefabReferenceField(const nlohmann::json& field)
{
  if (!field.is_object())
  {
    return false;
  }

  const auto type = field.find("type");
  const auto asset = field.find("asset");
  return type != field.end() && type->is_string() && type->get<std::string>() == "string"
    && asset != field.end() && asset->is_string() && asset->get<std::string>() == "prefab";
}

// The uuid a string field value holds, or the nil uuid for a value that is not a string or not a uuid.
[[nodiscard]] inline uuids::uuid scriptFieldUUID(const nlohmann::json& value)
{
  if (!value.is_string())
  {
    return {};
  }

  return uuids::uuid::from_string(value.get<std::string>()).value_or(uuids::uuid{});
}

#endif //SCRIPTFIELDASSET_H
