#ifndef TERRAINCREATETESTHELPERS_H
#define TERRAINCREATETESTHELPERS_H

#include "TerrainCreateFlow.h"

#include "assets/AssetRegistry.h"

#include <string>
#include <uuid.h>

namespace terrainCreateTest
{
  inline uuids::uuid uuidFrom(const std::string& text)
  {
    return uuids::uuid::from_string(text).value();
  }

  inline const auto existingUUID = uuidFrom("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
  inline const auto newTerrainUUID = uuidFrom("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");

  inline void addTerrainRecord(AssetRegistry& registry, const uuids::uuid& uuid, const std::string& displayName)
  {
    registry.registerAsset({ .uuid = uuid, .type = AssetType::Terrain, .path = terrainAssetPath(uuid),
                             .displayName = displayName });
  }

  inline AssetRegistry registryWithExistingTerrain()
  {
    AssetRegistry registry;
    addTerrainRecord(registry, existingUUID, "Existing");

    return registry;
  }

  inline TerrainCreateRequest validFlat()
  {
    TerrainCreateRequest request;
    request.name = "Ground";

    return request;
  }
}

#endif
