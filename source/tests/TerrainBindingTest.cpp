#include <gtest/gtest.h>

#include "ComponentRegistry.h"
#include "ComponentRegistration.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/collisions/Collider.h"
#include "objects/components/collisions/Terrain.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"
#include "terrain/TerrainBinding.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"

#include <memory>
#include <string>
#include <uuid.h>

namespace {
  uuids::uuid uuidFrom(const std::string& text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const auto mainUUID = uuidFrom("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
  const auto otherUUID = uuidFrom("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb");
  const auto terrainUUID = uuidFrom("11111111-1111-1111-1111-111111111111");
  const auto missingUUID = uuidFrom("22222222-2222-2222-2222-222222222222");

  struct World {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    SceneManager sceneManager;
    std::shared_ptr<SceneAsset> other;

    World()
    {
      registerDataComponents(*componentRegistry);

      const auto main = std::make_shared<SceneAsset>(mainUUID, "Main", componentRegistry);
      sceneManager.addScene(main);
      sceneManager.loadScene(main);

      other = std::make_shared<SceneAsset>(otherUUID, "Other", componentRegistry);
      sceneManager.addScene(other);
    }

    // The terrain sits on a child of an object in the scene that is not current.
    std::shared_ptr<Terrain> addTerrainChild(const uuids::uuid& uuid)
    {
      const auto parent = std::make_shared<Object>("Parent");
      other->getObjectManager()->addObject(parent);

      const auto child = std::make_shared<Object>("Child");
      child->setParent(parent);
      other->getObjectManager()->addObject(child);

      const auto terrain = std::make_shared<Terrain>();
      child->addComponent(terrain);
      terrain->setTerrainUUID(uuid);

      return terrain;
    }
  };
}

TEST(TerrainBinding, BindsATerrainInAChildOfANonCurrentScene)
{
  World world;
  terrain::TerrainStore store;
  const auto volume = store.create({ .uuid = terrainUUID });
  ASSERT_NE(volume, nullptr);

  const auto terrain = world.addTerrainChild(terrainUUID);
  EXPECT_EQ(terrain->volume(), nullptr);

  terrain::bindTerrainVolumes(store, world.sceneManager);

  EXPECT_EQ(terrain->volume(), volume);
}

TEST(TerrainBinding, ASecondCallWithNothingChangedLeavesTheBoundsCacheAlone)
{
  World world;
  terrain::TerrainStore store;
  ASSERT_NE(store.create({ .uuid = terrainUUID }), nullptr);

  const auto terrain = world.addTerrainChild(terrainUUID);
  const auto before = terrain->getGeometryRevision();

  terrain::bindTerrainVolumes(store, world.sceneManager);
  const auto bound = terrain->getGeometryRevision();
  EXPECT_GT(bound, before);

  terrain::bindTerrainVolumes(store, world.sceneManager);
  EXPECT_EQ(terrain->getGeometryRevision(), bound);
}

TEST(TerrainBinding, AComponentWhoseUUIDTheStoreLacksGetsNoVolume)
{
  World world;
  terrain::TerrainStore store;
  ASSERT_NE(store.create({ .uuid = terrainUUID }), nullptr);

  const auto bound = world.addTerrainChild(terrainUUID);
  const auto unbound = world.addTerrainChild(missingUUID);

  terrain::bindTerrainVolumes(store, world.sceneManager);

  EXPECT_NE(bound->volume(), nullptr);
  EXPECT_EQ(unbound->volume(), nullptr);
}

TEST(TerrainBinding, RebindingAfterARemovalDropsTheVolume)
{
  World world;
  terrain::TerrainStore store;
  const auto volume = store.create({ .uuid = terrainUUID });
  ASSERT_NE(volume, nullptr);

  const auto terrain = world.addTerrainChild(terrainUUID);
  terrain::bindTerrainVolumes(store, world.sceneManager);
  ASSERT_EQ(terrain->volume(), volume);

  ASSERT_TRUE(store.remove(terrainUUID));
  terrain::bindTerrainVolumes(store, world.sceneManager);

  EXPECT_EQ(terrain->volume(), nullptr);
}

TEST(TerrainBinding, ARemovedVolumeWithNoReferencesLeftResetsTheBoundsOnTheNextBind)
{
  World world;
  terrain::TerrainStore store;
  {
    const auto volume = store.create({ .uuid = terrainUUID });
    ASSERT_NE(volume, nullptr);
    volume->setChunk({ 0, 0, 0 }, terrain::TerrainChunk::uniform(-terrain::airDistance, 1));
  }

  const auto terrain = world.addTerrainChild(terrainUUID);
  terrain::bindTerrainVolumes(store, world.sceneManager);
  EXPECT_GT(terrain->getBoundingBox().maxX, 1.0f);

  ASSERT_TRUE(store.remove(terrainUUID));
  EXPECT_TRUE(terrain->volumeExpired());

  terrain::bindTerrainVolumes(store, world.sceneManager);

  EXPECT_FALSE(terrain->volumeExpired());
  EXPECT_EQ(terrain->volume(), nullptr);

  const auto& box = terrain->getBoundingBox();
  EXPECT_NEAR(box.minX, 0.0f, 1.0e-4f);
  EXPECT_NEAR(box.maxX, 0.0f, 1.0e-4f);
  EXPECT_NEAR(box.minY, 0.0f, 1.0e-4f);
  EXPECT_NEAR(box.maxY, 0.0f, 1.0e-4f);
  EXPECT_NEAR(box.minZ, 0.0f, 1.0e-4f);
  EXPECT_NEAR(box.maxZ, 0.0f, 1.0e-4f);

  const auto settled = terrain->getGeometryRevision();
  terrain::bindTerrainVolumes(store, world.sceneManager);
  EXPECT_EQ(terrain->getGeometryRevision(), settled);
}
