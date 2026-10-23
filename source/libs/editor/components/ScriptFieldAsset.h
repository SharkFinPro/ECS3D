#ifndef SCRIPTFIELDASSET_H
#define SCRIPTFIELDASSET_H

#include <assets/AssetRegistry.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <optional>
#include <string>

// The asset type a script field's uuid string names, so it is drawn as that type's slot. Empty for a field that
// is not a string or marks no asset a slot can hold.
[[nodiscard]] inline std::optional<AssetType> scriptFieldAssetType(const nlohmann::json& field)
{
  if (!field.is_object())
  {
    return std::nullopt;
  }

  const auto type = field.find("type");
  const auto asset = field.find("asset");
  if (type == field.end() || !type->is_string() || type->get<std::string>() != "string"
      || asset == field.end() || !asset->is_string())
  {
    return std::nullopt;
  }

  const auto kind = asset->get<std::string>();
  if (kind == "prefab")
  {
    return AssetType::Prefab;
  }

  if (kind == "model")
  {
    return AssetType::Model;
  }

  if (kind == "texture")
  {
    return AssetType::Texture;
  }

  return std::nullopt;
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
