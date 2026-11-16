#include <gtest/gtest.h>

#include "ComponentRegistry.h"
#include "ComponentRegistration.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/PlayerController.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/Terrain.h"
#include "terrain/TerrainStreamFocus.h"

#include <glm/vec3.hpp>
#include <memory>
#include <uuid.h>

namespace {
  const auto terrainUUID = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();
  const auto otherTerrainUUID = uuids::uuid::from_string("22222222-2222-2222-2222-222222222222").value();

  struct World {
    std::shared_ptr<ComponentRegistry> registry = std::make_shared<ComponentRegistry>();
    ObjectManager objects;

    World()
      : objects(registry)
    {
      registerDataComponents(*registry);
    }

    std::shared_ptr<Object> addObject(const glm::vec3& position, const glm::vec3& rotation = glm::vec3(0.0f),
                                      const glm::vec3& scale = glm::vec3(1.0f))
    {
      const auto object = std::make_shared<Object>("Object");
      const auto transform = object->getComponent<Transform>(ComponentType::transform);
      transform->setPosition(position);
      transform->setRotation(rotation);
      transform->setScale(scale);
      objects.addObject(object);
      return object;
    }

    std::shared_ptr<Object> addTerrainOwner(const uuids::uuid& terrain, const glm::vec3& position,
                                            const glm::vec3& rotation = glm::vec3(0.0f),
                                            const glm::vec3& scale = glm::vec3(1.0f))
    {
      const auto object = addObject(position, rotation, scale);
      const auto component = std::make_shared<Terrain>();
      component->setTerrainUUID(terrain);
      object->addComponent(component);
      return object;
    }

    std::shared_ptr<Object> addPlayer(const int32_t slot, const glm::vec3& position)
    {
      const auto object = addObject(position);
      const auto controller = std::make_shared<PlayerController>();
      controller->setPlayerSlot(slot);
      object->addComponent(controller);
      return object;
    }
  };

  void expectNear(const glm::vec3& actual, const glm::vec3& expected)
  {
    EXPECT_NEAR(actual.x, expected.x, 1.0e-4f);
    EXPECT_NEAR(actual.y, expected.y, 1.0e-4f);
    EXPECT_NEAR(actual.z, expected.z, 1.0e-4f);
  }
}

TEST(TerrainStreamFocusTest, AnIdentityOwnerOnlyDividesByTheVoxelSize)
{
  World world;
  world.addTerrainOwner(terrainUUID, glm::vec3(0.0f));

  expectNear(terrain::streamFocus(world.objects, terrainUUID, { 10.0f, 5.0f, -20.0f }, 2.5f), { 4.0f, 2.0f, -8.0f });
}

TEST(TerrainStreamFocusTest, ATranslatedOwnerMovesTheOrigin)
{
  World world;
  world.addTerrainOwner(terrainUUID, { 100.0f, 0.0f, -50.0f });

  expectNear(terrain::streamFocus(world.objects, terrainUUID, { 110.0f, 5.0f, -50.0f }, 2.0f), { 5.0f, 2.5f, 0.0f });
}

TEST(TerrainStreamFocusTest, ARotatedOwnerTurnsThePointIntoItsLocalFrame)
{
  World world;
  // Ninety degrees about Z takes local +X to world +Y.
  world.addTerrainOwner(terrainUUID, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 90.0f });

  expectNear(terrain::streamFocus(world.objects, terrainUUID, { 0.0f, 10.0f, 0.0f }, 1.0f), { 10.0f, 0.0f, 0.0f });

  // A point on world +X is local -Y, so the rotation is applied rather than ignored.
  expectNear(terrain::streamFocus(world.objects, terrainUUID, { 10.0f, 0.0f, 0.0f }, 1.0f), { 0.0f, -10.0f, 0.0f });
}

TEST(TerrainStreamFocusTest, TheOwnersScaleIsIgnored)
{
  World world;
  world.addTerrainOwner(terrainUUID, { 10.0f, 0.0f, 0.0f }, glm::vec3(0.0f), { 4.0f, 4.0f, 4.0f });

  expectNear(terrain::streamFocus(world.objects, terrainUUID, { 20.0f, 0.0f, 0.0f }, 1.0f), { 10.0f, 0.0f, 0.0f });
}

TEST(TerrainStreamFocusTest, WithoutAnOwnerTheWorldPointItselfIsUsed)
{
  World world;
  world.addTerrainOwner(otherTerrainUUID, { 100.0f, 0.0f, 0.0f });
  world.addObject({ 5.0f, 5.0f, 5.0f });

  expectNear(terrain::streamFocus(world.objects, terrainUUID, { 10.0f, 5.0f, -20.0f }, 2.5f), { 10.0f, 5.0f, -20.0f });

  // Positive control: the owner of the other terrain does move its own focus.
  expectNear(terrain::streamFocus(world.objects, otherTerrainUUID, { 110.0f, 0.0f, 0.0f }, 2.0f), { 5.0f, 0.0f, 0.0f });
}

TEST(TerrainStreamFocusTest, TheLowestUUIDAmongSeveralOwnersWins)
{
  World world;
  const auto first = world.addTerrainOwner(terrainUUID, { 100.0f, 0.0f, 0.0f });
  const auto second = world.addTerrainOwner(terrainUUID, { 200.0f, 0.0f, 0.0f });

  const auto lowest = first->getUUID() < second->getUUID() ? first : second;
  const float expectedX = 150.0f - lowest->getComponent<Transform>(ComponentType::transform)->getPosition().x;

  expectNear(terrain::streamFocus(world.objects, terrainUUID, { 150.0f, 0.0f, 0.0f }, 1.0f), { expectedX, 0.0f, 0.0f });
}

TEST(TerrainStreamFocusTest, APlayerIsFoundBySlot)
{
  World world;
  world.addPlayer(0, { 1.0f, 2.0f, 3.0f });
  world.addPlayer(1, { 10.0f, 20.0f, 30.0f });

  const auto zero = terrain::playerPosition(world.objects, 0);
  const auto one = terrain::playerPosition(world.objects, 1);
  ASSERT_TRUE(zero.has_value());
  ASSERT_TRUE(one.has_value());
  expectNear(*zero, { 1.0f, 2.0f, 3.0f });
  expectNear(*one, { 10.0f, 20.0f, 30.0f });
}

TEST(TerrainStreamFocusTest, NoPlayerWithTheSlotGivesNothing)
{
  World world;
  world.addPlayer(0, { 1.0f, 2.0f, 3.0f });
  world.addObject({ 9.0f, 9.0f, 9.0f });

  EXPECT_FALSE(terrain::playerPosition(world.objects, 5).has_value());
  EXPECT_TRUE(terrain::playerPosition(world.objects, 0).has_value());
}
