#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "ProjectPaths.h"
#include "ProjectSerializer.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/collisions/Terrain.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"
#include "terrain/TerrainBinding.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainFiles.h"
#include "terrain/TerrainFormat.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {
  uuids::uuid uuidFrom(const std::string& text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const auto sceneUuid = uuidFrom("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
  const auto savedUuid = uuidFrom("11111111-1111-4111-8111-111111111111");
  const auto unsavedUuid = uuidFrom("22222222-2222-4222-8222-222222222222");
  const auto corruptUuid = uuidFrom("33333333-3333-4333-8333-333333333333");

  AssetRecord terrainRecord(const uuids::uuid& uuid)
  {
    return { .uuid = uuid, .type = AssetType::Terrain, .path = terrainAssetPath(uuid) };
  }

  terrain::TerrainDescription savedDescription()
  {
    terrain::TerrainDescription description;
    description.uuid = savedUuid;
    description.voxelSize = 4.0f;
    description.palette.push_back({ "grass", savedUuid, uuids::uuid() });
    description.palette.push_back({ "rock", uuids::uuid(), savedUuid });
    return description;
  }

  std::shared_ptr<terrain::TerrainVolume> savedVolume()
  {
    auto volume = std::make_shared<terrain::TerrainVolume>(4.0f);
    volume->setChunk({ 0, 0, 0 }, terrain::TerrainChunk::uniform(-terrain::airDistance, 1));
    volume->setChunk({ 1, 0, 0 }, terrain::TerrainChunk::uniform(-terrain::airDistance, 2));
    volume->setChunk({ -3, 1, 2 }, terrain::TerrainChunk::uniform(-terrain::airDistance, 1));
    return volume;
  }

  // A project file shaped like a user's: one scene holding a Terrain component, and three Terrain records.
  bool writeProjectFile(const std::filesystem::path& root)
  {
    const auto componentRegistry = std::make_shared<ComponentRegistry>();
    registerDataComponents(*componentRegistry);

    AssetRegistry assetRegistry;
    SceneManager sceneManager;

    const auto scene = std::make_shared<SceneAsset>(sceneUuid, "Main", componentRegistry);
    sceneManager.addScene(scene);
    sceneManager.loadScene(scene);

    const auto object = std::make_shared<Object>("Ground");
    scene->getObjectManager()->addObject(object);
    const auto component = std::make_shared<Terrain>();
    object->addComponent(component);
    component->setTerrainUUID(savedUuid);

    assetRegistry.registerAsset(terrainRecord(savedUuid));
    assetRegistry.registerAsset(terrainRecord(unsavedUuid));
    assetRegistry.registerAsset(terrainRecord(corruptUuid));

    const ProjectSerializer serializer(&assetRegistry, &sceneManager, componentRegistry);
    return serializer.save(root / projectFileName);
  }

  bool writeSampleProject(const std::filesystem::path& root)
  {
    std::filesystem::create_directories(root);

    if (!writeProjectFile(root))
    {
      return false;
    }

    if (!terrain::saveTerrain(root, savedDescription(), *savedVolume()).ok)
    {
      return false;
    }

    const auto corruptFolder = terrain::terrainFolder(root, corruptUuid);
    std::filesystem::create_directories(corruptFolder);
    std::ofstream(corruptFolder / terrain::indexFileName) << "{ not json";
    return true;
  }

  class TerrainProjectLoadTest : public testing::Test
  {
  protected:
    void SetUp() override
    {
      std::random_device device;
      m_root = std::filesystem::temp_directory_path() / ("ecs3d_terrain_project_" + std::to_string(device()));
      std::filesystem::remove_all(m_root);
      std::filesystem::create_directories(m_root);
    }

    void TearDown() override
    {
      std::error_code ignored;
      std::filesystem::remove_all(m_root, ignored);
    }

    std::filesystem::path m_root;
  };

  struct LoadedProject
  {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    AssetRegistry assetRegistry;
    SceneManager sceneManager;

    explicit LoadedProject(const std::filesystem::path& root)
    {
      registerDataComponents(*componentRegistry);
      const ProjectSerializer serializer(&assetRegistry, &sceneManager, componentRegistry);
      loaded = serializer.load(pathToUtf8(root / projectFileName));
    }

    bool loaded = false;
  };
}

TEST_F(TerrainProjectLoadTest, LoadsSavedTerrainsAndLeavesTheRestEmpty)
{
  ASSERT_TRUE(writeSampleProject(m_root));

  LoadedProject project(m_root);
  ASSERT_TRUE(project.loaded);

  terrain::TerrainStore store;
  store.reconcile(project.assetRegistry);
  ASSERT_EQ(store.terrains().size(), 3u);

  // Positive control: before the load every terrain is empty, the saved one included.
  ASSERT_NE(store.find(savedUuid), nullptr);
  EXPECT_TRUE(store.find(savedUuid)->chunks().empty());

  const auto report = terrain::loadProjectTerrains(m_root, project.assetRegistry, store);

  EXPECT_EQ(report.loaded, 1u);
  EXPECT_EQ(report.missing, 1u);
  ASSERT_EQ(report.errors.size(), 1u);
  EXPECT_NE(report.errors[0].find(pathToUtf8(terrain::terrainFolder(m_root, corruptUuid) / terrain::indexFileName)),
            std::string::npos)
    << report.errors[0];

  const auto expected = savedVolume();
  const auto loaded = store.find(savedUuid);
  ASSERT_NE(loaded, nullptr);
  ASSERT_EQ(loaded->chunks().size(), expected->chunks().size());

  for (const auto& [coord, chunk] : expected->chunks())
  {
    const auto it = loaded->chunks().find(coord);
    ASSERT_NE(it, loaded->chunks().end());
    EXPECT_EQ(it->second->hash(), chunk->hash());
  }

  const auto* description = store.description(savedUuid);
  ASSERT_NE(description, nullptr);
  EXPECT_NEAR(description->voxelSize, 4.0f, 1.0e-6f);
  ASSERT_EQ(description->palette.size(), 2u);
  EXPECT_EQ(description->palette[0].name, "grass");
  EXPECT_EQ(description->palette[1].name, "rock");
  EXPECT_EQ(description->palette[1].specularTexture, savedUuid);

  for (const auto& uuid : { unsavedUuid, corruptUuid })
  {
    ASSERT_NE(store.find(uuid), nullptr);
    EXPECT_TRUE(store.find(uuid)->chunks().empty());
  }

  EXPECT_EQ(store.terrains().size(), 3u);
}

TEST_F(TerrainProjectLoadTest, ATerrainComponentGetsItsBoundsFromTheLoadedVolume)
{
  ASSERT_TRUE(writeSampleProject(m_root));

  LoadedProject project(m_root);
  ASSERT_TRUE(project.loaded);

  const auto scene = project.sceneManager.getCurrentScene();
  ASSERT_NE(scene, nullptr);

  std::shared_ptr<Terrain> component;
  for (const auto& object : scene->getObjectManager()->getAllObjects())
  {
    if (const auto found = object->getComponent<Terrain>(ComponentType::collider))
    {
      component = found;
    }
  }

  ASSERT_NE(component, nullptr);
  EXPECT_EQ(component->getTerrainUUID(), savedUuid);

  terrain::TerrainStore store;
  store.reconcile(project.assetRegistry);
  terrain::bindTerrainVolumes(store, project.sceneManager);

  // Positive control: an empty terrain has no bounds, so every support point is the owner's position.
  const glm::vec3 position = component->findFurthestPoint({ 1.0f, 1.0f, 1.0f });
  EXPECT_NEAR(glm::length(component->findFurthestPoint({ -1.0f, 1.0f, -1.0f }) - position), 0.0f, 1.0e-4f);

  ASSERT_EQ(terrain::loadProjectTerrains(m_root, project.assetRegistry, store).loaded, 1u);
  terrain::bindTerrainVolumes(store, project.sceneManager);
  EXPECT_EQ(component->volume(), store.find(savedUuid));
  ASSERT_FALSE(store.find(savedUuid)->occupiedBounds().empty());

  EXPECT_GT(glm::length(component->findFurthestPoint({ 1.0f, 1.0f, 1.0f }) - position), 1.0f);
}

TEST_F(TerrainProjectLoadTest, AnEmptyRootLoadsNothing)
{
  AssetRegistry assetRegistry;
  assetRegistry.registerAsset(terrainRecord(savedUuid));
  terrain::TerrainStore store;
  store.reconcile(assetRegistry);

  const auto report = terrain::loadProjectTerrains({}, assetRegistry, store);

  EXPECT_EQ(report.loaded, 0u);
  EXPECT_EQ(report.missing, 0u);
  EXPECT_TRUE(report.errors.empty());

  // Positive control: the same registry with a real root reports the unsaved terrain.
  EXPECT_EQ(terrain::loadProjectTerrains(m_root, assetRegistry, store).missing, 1u);
}

// A tool for manual checks: writes the sample project into the folder named by ECS3D_TERRAIN_SAMPLE_DIR and is
// skipped when it is unset. Run it with --gtest_also_run_disabled_tests.
TEST_F(TerrainProjectLoadTest, DISABLED_WriteSampleProject)
{
  const char* directory = std::getenv("ECS3D_TERRAIN_SAMPLE_DIR");

  if (directory == nullptr || *directory == '\0')
  {
    GTEST_SKIP() << "ECS3D_TERRAIN_SAMPLE_DIR is not set";
  }

  EXPECT_TRUE(writeSampleProject(std::filesystem::path(directory)));
}
