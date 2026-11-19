#include <gtest/gtest.h>

#include "ComponentRegistry.h"
#include "ComponentRegistration.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/collisions/Collider.h"
#include "objects/components/collisions/SphereCollider.h"
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

TEST(TerrainBinding, BindsTheTerrainAmongObjectsWithOtherColliders)
{
  World world;
  terrain::TerrainStore store;
  const auto volume = store.create({ .uuid = terrainUUID });
  ASSERT_NE(volume, nullptr);

  const auto sphereObject = std::make_shared<Object>("Sphere");
  const auto sphere = std::make_shared<SphereCollider>();
  sphereObject->addComponent(sphere);
  world.other->getObjectManager()->addObject(sphereObject);

  world.other->getObjectManager()->addObject(std::make_shared<Object>("Bare"));

  const auto terrain = world.addTerrainChild(terrainUUID);
  terrain::bindTerrainVolumes(store, world.sceneManager);

  EXPECT_EQ(terrain->volume(), volume);
  EXPECT_EQ(sphereObject->getComponent<SphereCollider>(ComponentType::collider), sphere);
}

TEST(TerrainBinder, BindsATerrainAddedAfterTheFirstBind)
{
  World world;
  terrain::TerrainStore store;
  const auto volume = store.create({ .uuid = terrainUUID });
  ASSERT_NE(volume, nullptr);

  terrain::TerrainBinder binder;
  binder.bind(store, world.sceneManager);

  const auto terrain = world.addTerrainChild(terrainUUID);
  binder.bind(store, world.sceneManager);

  EXPECT_EQ(terrain->volume(), volume);
}

TEST(TerrainBinder, BindsATerrainAddedToAnObjectAlreadyInTheScene)
{
  World world;
  terrain::TerrainStore store;
  const auto volume = store.create({ .uuid = terrainUUID });
  ASSERT_NE(volume, nullptr);

  const auto object = std::make_shared<Object>("Ground");
  world.other->getObjectManager()->addObject(object);

  terrain::TerrainBinder binder;
  binder.bind(store, world.sceneManager);

  const auto terrain = std::make_shared<Terrain>();
  terrain->setTerrainUUID(terrainUUID);
  object->addComponent(terrain);
  binder.bind(store, world.sceneManager);

  EXPECT_EQ(terrain->volume(), volume);
}

TEST(TerrainBinder, FollowsAUUIDChangeOnACachedTerrain)
{
  World world;
  terrain::TerrainStore store;
  const auto first = store.create({ .uuid = terrainUUID });
  const auto second = store.create({ .uuid = missingUUID });
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);

  const auto terrain = world.addTerrainChild(terrainUUID);
  terrain::TerrainBinder binder;
  binder.bind(store, world.sceneManager);
  ASSERT_EQ(terrain->volume(), first);

  terrain->setTerrainUUID(missingUUID);
  binder.bind(store, world.sceneManager);

  EXPECT_EQ(terrain->volume(), second);
}

TEST(TerrainBinder, FollowsAStoreChangeOnACachedTerrain)
{
  World world;
  terrain::TerrainStore store;
  ASSERT_NE(store.create({ .uuid = terrainUUID }), nullptr);

  const auto terrain = world.addTerrainChild(terrainUUID);
  terrain::TerrainBinder binder;
  binder.bind(store, world.sceneManager);
  ASSERT_NE(terrain->volume(), nullptr);

  ASSERT_TRUE(store.remove(terrainUUID));
  binder.bind(store, world.sceneManager);

  EXPECT_EQ(terrain->volume(), nullptr);
  EXPECT_FALSE(terrain->volumeExpired());
}

TEST(TerrainBinder, BindsATerrainInASceneAddedAfterTheFirstBind)
{
  World world;
  terrain::TerrainStore store;
  const auto volume = store.create({ .uuid = terrainUUID });
  ASSERT_NE(volume, nullptr);

  // Built before the first bind, so only the new scene tells the binder to look again.
  const auto late = std::make_shared<SceneAsset>(uuidFrom("cccccccc-cccc-cccc-cccc-cccccccccccc"), "Late",
                                                 world.componentRegistry);
  const auto object = std::make_shared<Object>("Ground");
  const auto terrain = std::make_shared<Terrain>();
  terrain->setTerrainUUID(terrainUUID);
  object->addComponent(terrain);
  late->getObjectManager()->addObject(object);

  terrain::TerrainBinder binder;
  binder.bind(store, world.sceneManager);
  EXPECT_EQ(terrain->volume(), nullptr);

  world.sceneManager.addScene(late);
  binder.bind(store, world.sceneManager);

  EXPECT_EQ(terrain->volume(), volume);
}

TEST(TerrainBinder, DropsATerrainRemovedFromItsObject)
{
  World world;
  terrain::TerrainStore store;
  ASSERT_NE(store.create({ .uuid = terrainUUID }), nullptr);

  const auto terrain = world.addTerrainChild(terrainUUID);
  terrain::TerrainBinder binder;
  binder.bind(store, world.sceneManager);

  terrain->getOwner()->removeComponent(terrain);
  terrain->setVolume({});
  binder.bind(store, world.sceneManager);

  EXPECT_EQ(terrain->volume(), nullptr);
}

TEST(ObjectManagerStructureRevision, AdvancesOnObjectAndColliderChangesOnly)
{
  World world;
  const auto& objects = world.other->getObjectManager();

  auto before = ObjectManager::structureRevision();
  const auto object = std::make_shared<Object>("Thing");
  objects->addObject(object);
  EXPECT_NE(ObjectManager::structureRevision(), before);

  before = ObjectManager::structureRevision();
  const auto sphere = std::make_shared<SphereCollider>();
  object->addComponent(sphere);
  EXPECT_NE(ObjectManager::structureRevision(), before);

  before = ObjectManager::structureRevision();
  object->removeComponent(sphere);
  EXPECT_NE(ObjectManager::structureRevision(), before);

  terrain::TerrainStore store;
  terrain::TerrainBinder binder;
  binder.bind(store, world.sceneManager);
  before = ObjectManager::structureRevision();
  binder.bind(store, world.sceneManager);
  EXPECT_EQ(ObjectManager::structureRevision(), before);
}
