#ifndef COINRUSHTESTHELPERS_H
#define COINRUSHTESTHELPERS_H

#include <gtest/gtest.h>

#include "TemplateTestHelpers.h"
#include "assets/AssetRegistry.h"
#include "objects/ObjectManager.h"
#include "scenes/SceneAsset.h"

#include <nlohmann/json.hpp>
#include <uuid.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

// Shared by the suites that check the Coin Rush sample folder: where it is, the ids it uses, and lookups
// in its project and scripts.
namespace coinrushTest
{
  using namespace templateTest;

  inline const std::filesystem::path templateFolder = sourceRoot / "templates" / "coin-rush";

  inline constexpr const char* gridCheckerTexture = "48ad67dd-957f-4fc9-bd7b-019debffa1d0";
  inline constexpr const char* boulderModel = "9d0e47fd-8a6f-4ff9-9614-ddb0c5dddd5a";
  inline constexpr const char* torusModel = "7a1cd753-3775-44a0-858f-27239f8740f5";
  inline constexpr const char* paletteYellowTexture = "1d5edebf-57f9-499c-9840-b84a57174422";
  inline constexpr const char* playerPrefab = "9c7f0e83-4d16-4b52-a09e-27f5b3d81c6a";
  inline constexpr float dt = 1.0f / 50.0f;

  // Palette red, blue, green and orange: the textures of player slots 0 to 3.
  inline const std::vector<std::string> slotTextures = {
    "7c8c01db-771f-4048-94c2-004b8d19e7aa", "66a8a6fc-d151-4567-9ce5-2e17cc675fe0",
    "c5eee5e6-9bd0-4f9b-977f-b9220a21a630", "ea184ad8-ca64-48cd-b69e-92bcb4649158"
  };

  inline bool approx(const float a, const float b)
  {
    return std::abs(a - b) < 1e-4f;
  }

  inline std::set<std::string> rootNames(const SceneAsset& scene)
  {
    std::set<std::string> names;
    for (const auto& object : scene.getObjectManager()->getObjects())
    {
      names.insert(object->getName());
    }

    return names;
  }

  inline nlohmann::json scriptNamed(const nlohmann::json& object, const std::string& className)
  {
    for (const auto& entry : object.at("scripts"))
    {
      if (entry.at("className") == className)
      {
        return entry;
      }
    }

    ADD_FAILURE() << object.at("name") << " has no script " << className;
    return nlohmann::json::object();
  }

  inline nlohmann::json fieldOf(const nlohmann::json& script, const std::string& name)
  {
    for (const auto& field : script.at("fields"))
    {
      if (field.at("name") == name)
      {
        return field.at("value");
      }
    }

    ADD_FAILURE() << script.at("className") << " has no field " << name;
    return nullptr;
  }

  inline nlohmann::json prefabBody(const LoadedTemplate& project, const std::string& uuid)
  {
    return project.assetRegistry.getPrefabBody(uuids::uuid::from_string(uuid).value());
  }

  inline nlohmann::json prefabNamed(const nlohmann::json& project, const std::string& name)
  {
    for (const auto& prefab : project.at("assets").at("prefabs"))
    {
      if (prefab.at("name") == name)
      {
        return prefab;
      }
    }

    ADD_FAILURE() << "no prefab named " << name;
    return nlohmann::json::object();
  }

  // The ball collider's radius at the prefab's scale, which is what the narrow phase uses.
  inline float boulderWorldRadius(const nlohmann::json& body)
  {
    const auto scale = scaleOfJson(body);
    const float widest = std::max({ std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });

    return widest * componentOf(body, "Collider", "Sphere").at("radius").get<float>();
  }

  inline std::string textureOf(const nlohmann::json& object)
  {
    return componentOf(object, "ModelRenderer").at("textureUUID").get<std::string>();
  }
}

#endif //COINRUSHTESTHELPERS_H
