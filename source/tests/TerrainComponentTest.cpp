#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "TestScene.h"
#include "CollisionSystem.h"
#include "WireTypes.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/Collider.h"
#include "objects/components/collisions/Terrain.h"
#include "queries/SceneQueries.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainVolume.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <uuid.h>

namespace {
  using fixtures::makeScene;
  using fixtures::Scene;

  constexpr float voxelSize = 2.0f;
  constexpr float tolerance = 1e-3f;
  constexpr float dt = 1.0f / 50.0f;
  constexpr uint32_t allLayers = 0xFFFFFFFFu;

  uuids::uuid someUUID()
  {
    return uuids::uuid::from_string("12345678-1234-1234-1234-123456789abc").value();
  }

  std::shared_ptr<terrain::TerrainVolume> volumeWithChunk(const terrain::ChunkCoord coord)
  {
    auto volume = std::make_shared<terrain::TerrainVolume>(voxelSize);
    volume->setChunk(coord, terrain::TerrainChunk::uniform(-terrain::airDistance, 1));

    return volume;
  }

  struct TerrainObject {
    std::shared_ptr<Object> object;
    std::shared_ptr<Terrain> terrain;
  };

  TerrainObject addTerrain(const Scene& scene, const glm::vec3& position = glm::vec3(0))
  {
    auto object = fixtures::addObject(scene, "Terrain", position);
    auto terrain = std::make_shared<Terrain>();
    object->addComponent(terrain);

    return { object, terrain };
  }

  void setNonDefaults(Terrain& terrain)
  {
    terrain.setTerrainUUID(someUUID());
    terrain.setCollisionEnabled(false);
    terrain.setCastShadows(false);
    terrain.setRenderCollider(true);
    terrain.setLayer(7);
    terrain.setMask(0x0F0Fu);
  }

  void expectSameSettings(Terrain& actual, Terrain& expected)
  {
    EXPECT_EQ(actual.getTerrainUUID(), expected.getTerrainUUID());
    EXPECT_EQ(actual.getCollisionEnabled(), expected.getCollisionEnabled());
    EXPECT_EQ(actual.getCastShadows(), expected.getCastShadows());
    EXPECT_EQ(actual.getRenderCollider(), expected.getRenderCollider());
    EXPECT_EQ(actual.isTrigger(), expected.isTrigger());
    EXPECT_EQ(actual.getLayer(), expected.getLayer());
    EXPECT_EQ(actual.getMask(), expected.getMask());
  }

  // The fields a valid terrain payload carries, in pack order after the discriminator.
  net::Message wirePayload(const bool isTrigger, const uint32_t layer)
  {
    net::Message message(net::MessageType::undefined);
    message.write(true);
    message.write(someUUID());
    message.write(false);
    message.write(false);
    message.write(isTrigger);
    message.write(layer);
    message.write(uint32_t{ 0x0F0Fu });

    return message;
  }

  void unpackInto(Terrain& terrain, const net::Message& message)
  {
    net::MessageReader reader(message);
    terrain.unpack(reader);
  }

  void expectBounds(const BoundingBox& bounds, const glm::vec3& low, const glm::vec3& high)
  {
    EXPECT_NEAR(bounds.minX, low.x, tolerance);
    EXPECT_NEAR(bounds.minY, low.y, tolerance);
    EXPECT_NEAR(bounds.minZ, low.z, tolerance);
    EXPECT_NEAR(bounds.maxX, high.x, tolerance);
    EXPECT_NEAR(bounds.maxY, high.y, tolerance);
    EXPECT_NEAR(bounds.maxZ, high.z, tolerance);
  }

  bool refused(Terrain& terrain, const nlohmann::json& payload)
  {
    try
    {
      terrain.loadFromJSON(payload);
    }
    catch (const RefusedPayload&)
    {
      return true;
    }

    return false;
  }
}

TEST(TerrainComponentTest, StartsWithTheDocumentedDefaults)
{
  const Terrain terrain;

  EXPECT_EQ(terrain.getColliderType(), ColliderType::terrainCollider);
  EXPECT_EQ(terrain.getType(), ComponentType::collider);
  EXPECT_EQ(terrain.getSubType(), ComponentType::SubComponentType_terrain);
  EXPECT_EQ(terrain.getPackedType(), ComponentType::SubComponentType_terrain);
  EXPECT_TRUE(terrain.getTerrainUUID().is_nil());
  EXPECT_TRUE(terrain.getCollisionEnabled());
  EXPECT_TRUE(terrain.getCastShadows());
  EXPECT_FALSE(terrain.isTrigger());
  EXPECT_EQ(terrain.volume(), nullptr);
}

TEST(TerrainComponentTest, TheRegistryCreatesOneByItsKey)
{
  ComponentRegistry registry;
  registerDataComponents(registry);

  const auto component = registry.create("Terrain");
  ASSERT_NE(component, nullptr);
  EXPECT_NE(std::dynamic_pointer_cast<Terrain>(component), nullptr);
  EXPECT_EQ(componentTypeToRegistryKey.at(ComponentType::SubComponentType_terrain), "Terrain");
  EXPECT_EQ(componentTypeToString.at(ComponentType::SubComponentType_terrain), "Terrain");
  EXPECT_EQ(subComponentTypeToParent.at(ComponentType::SubComponentType_terrain), ComponentType::collider);

  // A positive control for the wire discriminator: it sits after every existing type and below the sentinel.
  EXPECT_GT(static_cast<uint8_t>(ComponentType::SubComponentType_terrain),
            static_cast<uint8_t>(ComponentType::SubComponentType_convexMeshCollider));
  EXPECT_LT(static_cast<uint8_t>(ComponentType::SubComponentType_terrain),
            static_cast<uint8_t>(ComponentType::count));
}

TEST(TerrainComponentTest, ItRoundTripsThroughJson)
{
  Terrain source;
  setNonDefaults(source);

  Terrain loaded;
  loaded.loadFromJSON(source.serialize());

  expectSameSettings(loaded, source);
  EXPECT_EQ(loaded.getTerrainUUID(), someUUID());
  EXPECT_FALSE(loaded.getCollisionEnabled());
  EXPECT_FALSE(loaded.getCastShadows());
  EXPECT_EQ(loaded.getLayer(), 7u);
  EXPECT_EQ(loaded.getMask(), 0x0F0Fu);
  EXPECT_EQ(loaded.serialize()["subType"], "Terrain");
}

TEST(TerrainComponentTest, ItRoundTripsThroughThePacker)
{
  Terrain source;
  setNonDefaults(source);

  net::Message message(net::MessageType::undefined);
  source.pack(message);

  Terrain unpacked;
  net::MessageReader reader(message);
  EXPECT_EQ(reader.read<ComponentType>(), ComponentType::SubComponentType_terrain);
  unpacked.unpack(reader);

  expectSameSettings(unpacked, source);
  EXPECT_EQ(unpacked.getTerrainUUID(), someUUID());
  EXPECT_EQ(unpacked.getMask(), 0x0F0Fu);
  EXPECT_EQ(reader.remaining(), 0u);
}

TEST(TerrainComponentTest, ANilTerrainUuidSerializesAsEmptyAndLoadsBackNil)
{
  Terrain source;
  const auto json = source.serialize();
  EXPECT_EQ(json["terrainUUID"], "");

  Terrain loaded;
  loaded.setTerrainUUID(someUUID());
  loaded.loadFromJSON(json);
  EXPECT_TRUE(loaded.getTerrainUUID().is_nil());
}

TEST(TerrainComponentTest, EveryMalformedJsonPayloadIsRefusedAndChangesNothing)
{
  Terrain terrain;
  setNonDefaults(terrain);
  const auto before = terrain.serialize();

  const auto valid = [&before](const std::string& key, const nlohmann::json& value)
  {
    auto payload = before;
    payload[key] = value;

    return payload;
  };

  EXPECT_TRUE(refused(terrain, valid("terrainUUID", "not-a-uuid")));
  EXPECT_TRUE(refused(terrain, valid("terrainUUID", 5)));
  EXPECT_TRUE(refused(terrain, valid("collisionEnabled", "yes")));
  EXPECT_TRUE(refused(terrain, valid("castShadows", 1)));
  EXPECT_TRUE(refused(terrain, valid("renderCollider", "true")));
  EXPECT_TRUE(refused(terrain, valid("layer", -1)));
  EXPECT_TRUE(refused(terrain, valid("layer", "3")));
  EXPECT_TRUE(refused(terrain, valid("mask", 1.5)));
  EXPECT_TRUE(refused(terrain, valid("mask", 4294967296ull)));
  EXPECT_TRUE(refused(terrain, valid("isTrigger", true)));
  EXPECT_TRUE(refused(terrain, nlohmann::json::array()));

  EXPECT_EQ(terrain.serialize(), before);

  // The same payload with a valid value applies, so the refusals above came from the bad field.
  auto good = before;
  good["layer"] = 9;
  good["collisionEnabled"] = true;
  EXPECT_FALSE(refused(terrain, good));
  EXPECT_EQ(terrain.getLayer(), 9u);
  EXPECT_TRUE(terrain.getCollisionEnabled());
}

TEST(TerrainComponentTest, AMalformedWirePayloadIsRefusedAndChangesNothing)
{
  Terrain terrain;
  terrain.setLayer(3);
  const auto before = terrain.serialize();

  EXPECT_THROW(unpackInto(terrain, wirePayload(true, 5)), RefusedPayload);
  EXPECT_EQ(terrain.serialize(), before);

  unpackInto(terrain, wirePayload(false, 5));
  EXPECT_EQ(terrain.getLayer(), 5u);
  EXPECT_EQ(terrain.getTerrainUUID(), someUUID());
  EXPECT_FALSE(terrain.getCollisionEnabled());
}

TEST(TerrainComponentTest, AnOutOfRangeLayerIsClampedOnLoadAndOffTheWire)
{
  Terrain fromJson;
  auto saved = fromJson.serialize();
  saved["layer"] = 99u;
  fromJson.loadFromJSON(saved);
  EXPECT_EQ(fromJson.getLayer(), 31u);

  Terrain fromWire;
  unpackInto(fromWire, wirePayload(false, 32));
  EXPECT_EQ(fromWire.getLayer(), 31u);

  unpackInto(fromWire, wirePayload(false, 30));
  EXPECT_EQ(fromWire.getLayer(), 30u);
}

TEST(TerrainComponentTest, ATerrainCanNeverBeATrigger)
{
  Terrain terrain;
  terrain.setIsTrigger(true);
  EXPECT_FALSE(terrain.isTrigger());

  BoxCollider box;
  box.setIsTrigger(true);
  EXPECT_TRUE(box.isTrigger());
}

TEST(TerrainComponentTest, ItsBoundsAreTheOccupiedBoxUnderATranslatedAndRotatedOwner)
{
  const auto scene = makeScene();
  const auto [object, comp] = addTerrain(scene, { 10, 0, 0 });
  fixtures::transformOf(object)->setRotation({ 0, 0, 90 });

  const auto volume = volumeWithChunk({ 1, 0, 0 });
  comp->setVolume(volume);
  EXPECT_EQ(comp->volume(), volume);

  // Local box x 64..128, y 0..64, z 0..64; a quarter turn about z sends (x, y) to (-y, x).
  expectBounds(comp->getBoundingBox(), { -54, 64, 0 }, { 10, 128, 64 });
}

TEST(TerrainComponentTest, ItsBoundsIgnoreTheOwnersScale)
{
  const auto scene = makeScene();
  const auto [object, comp] = addTerrain(scene);
  const auto volume = volumeWithChunk({ 0, 0, 0 });
  comp->setVolume(volume);

  expectBounds(comp->getBoundingBox(), { 0, 0, 0 }, { 64, 64, 64 });

  fixtures::transformOf(object)->setScale({ 3, 3, 3 });
  expectBounds(comp->getBoundingBox(), { 0, 0, 0 }, { 64, 64, 64 });
}

TEST(TerrainComponentTest, WithoutAVolumeTheBoundsAreADegenerateBoxAtTheOwner)
{
  const auto scene = makeScene();
  const auto [object, comp] = addTerrain(scene, { 4, 5, 6 });

  expectBounds(comp->getBoundingBox(), { 4, 5, 6 }, { 4, 5, 6 });

  // The same terrain with a volume is not degenerate, so the box above came from the missing volume.
  const auto volume = volumeWithChunk({ 0, 0, 0 });
  comp->setVolume(volume);
  expectBounds(comp->getBoundingBox(), { 4, 5, 6 }, { 68, 69, 70 });

  // A volume that is gone reads as none.
  comp->setVolume(std::weak_ptr<const terrain::TerrainVolume>());
  expectBounds(comp->getBoundingBox(), { 4, 5, 6 }, { 4, 5, 6 });
}

TEST(TerrainComponentTest, AnEmptyVolumeGivesADegenerateBox)
{
  const auto scene = makeScene();
  const auto [object, comp] = addTerrain(scene, { 1, 2, 3 });
  const auto volume = std::make_shared<terrain::TerrainVolume>(voxelSize);
  comp->setVolume(volume);

  expectBounds(comp->getBoundingBox(), { 1, 2, 3 }, { 1, 2, 3 });
}

TEST(TerrainComponentTest, InvalidateBoundsRefreshesTheBoxAfterAVolumeEdit)
{
  const auto scene = makeScene();
  const auto [object, comp] = addTerrain(scene);
  const auto volume = volumeWithChunk({ 0, 0, 0 });
  comp->setVolume(volume);
  expectBounds(comp->getBoundingBox(), { 0, 0, 0 }, { 64, 64, 64 });

  volume->setChunk({ 1, 0, 0 }, terrain::TerrainChunk::uniform(-terrain::airDistance, 1));

  // Nothing moved, so the cached box is kept: the cache is real.
  expectBounds(comp->getBoundingBox(), { 0, 0, 0 }, { 64, 64, 64 });

  comp->invalidateBounds();
  expectBounds(comp->getBoundingBox(), { 0, 0, 0 }, { 128, 64, 64 });
}

TEST(TerrainComponentTest, ChangingTheTerrainUuidInvalidatesTheBox)
{
  const auto scene = makeScene();
  const auto [object, comp] = addTerrain(scene);
  const auto volume = volumeWithChunk({ 0, 0, 0 });
  comp->setVolume(volume);
  expectBounds(comp->getBoundingBox(), { 0, 0, 0 }, { 64, 64, 64 });

  const auto revision = comp->getGeometryRevision();
  comp->setTerrainUUID(someUUID());
  EXPECT_GT(comp->getGeometryRevision(), revision);
  EXPECT_EQ(comp->volume(), nullptr);
  expectBounds(comp->getBoundingBox(), { 0, 0, 0 }, { 0, 0, 0 });
}

TEST(TerrainComponentTest, ALoadedUuidChangeDropsTheVolumeButTheSameUuidKeepsIt)
{
  const auto scene = makeScene();
  const auto [object, comp] = addTerrain(scene);
  comp->setTerrainUUID(someUUID());
  const auto volume = volumeWithChunk({ 0, 0, 0 });
  comp->setVolume(volume);

  auto payload = comp->serialize();
  comp->loadFromJSON(payload);
  EXPECT_EQ(comp->volume(), volume);
  expectBounds(comp->getBoundingBox(), { 0, 0, 0 }, { 64, 64, 64 });

  payload["terrainUUID"] = "";
  comp->loadFromJSON(payload);
  EXPECT_EQ(comp->volume(), nullptr);
  expectBounds(comp->getBoundingBox(), { 0, 0, 0 }, { 0, 0, 0 });

  comp->setVolume(volume);
  unpackInto(*comp, wirePayload(false, 5));
  EXPECT_EQ(comp->volume(), nullptr);
}

TEST(TerrainComponentTest, AParentedTerrainHasTheBoxUnderTheCombinedTransform)
{
  const auto scene = makeScene();
  const auto parent = fixtures::addObject(scene, "Parent", { 5, 0, 0 });
  fixtures::transformOf(parent)->setRotation({ 0, 0, 90 });

  const auto child = fixtures::addChildObject(scene, "Child", parent);
  const auto comp = std::make_shared<Terrain>();
  child->addComponent(comp);
  const auto volume = volumeWithChunk({ 0, 0, 0 });
  comp->setVolume(volume);

  // Local box 0..64 on every axis; the parent's quarter turn about z sends (x, y) to (-y, x).
  expectBounds(comp->getBoundingBox(), { -59, 0, 0 }, { 5, 64, 64 });

  // The same child with the parent unrotated is a different box, so the rotation above was applied.
  fixtures::transformOf(parent)->setRotation({ 0, 0, 0 });
  expectBounds(comp->getBoundingBox(), { 5, 0, 0 }, { 69, 64, 64 });
}

TEST(TerrainComponentTest, AnObjectWithATerrainRoundTripsThroughThePackerAndJson)
{
  const auto scene = makeScene();
  const auto [object, comp] = addTerrain(scene, { 1, 2, 3 });
  setNonDefaults(*comp);

  net::Message message(net::MessageType::objectSpawned);
  object->pack(message);

  const auto packedTarget = std::make_shared<Object>("Target");
  scene.objectManager->addObject(packedTarget);
  net::MessageReader reader(message);
  packedTarget->unpack(reader);

  const auto fromWire = packedTarget->getComponent<Terrain>(ComponentType::collider);
  ASSERT_NE(fromWire, nullptr);
  expectSameSettings(*fromWire, *comp);
  EXPECT_EQ(reader.remaining(), 0u);
  fixtures::expectNear(fixtures::positionOf(packedTarget), { 1, 2, 3 }, tolerance);

  const auto json = object->serialize();
  const auto jsonTarget = std::make_shared<Object>(json, scene.objectManager.get());

  const auto fromJson = jsonTarget->getComponent<Terrain>(ComponentType::collider);
  ASSERT_NE(fromJson, nullptr);
  expectSameSettings(*fromJson, *comp);
  fixtures::expectNear(fixtures::positionOf(jsonTarget), { 1, 2, 3 }, tolerance);
}

namespace {
  struct CollisionRun {
    bool terrainPaired = false;
    bool controlPaired = false;
    glm::vec3 moverPosition{ 0 };
  };

  // A dynamic box inside a terrain's bounds, and, far away, a dynamic box overlapping an ordinary static one.
  CollisionRun runCollision(const bool terrainCollisionEnabled)
  {
    const auto scene = makeScene();

    const auto [terrainObject, comp] = addTerrain(scene);
    const auto volume = volumeWithChunk({ 0, 0, 0 });
    comp->setVolume(volume);
    comp->setCollisionEnabled(terrainCollisionEnabled);

    const auto mover = fixtures::addObject(scene, "Mover", { 30, 30, 30 });
    fixtures::addBoxCollider(mover);
    fixtures::addRigidBody(mover);

    const auto controlMover = fixtures::addObject(scene, "ControlMover", { 300, 0, 0 });
    fixtures::addBoxCollider(controlMover);
    fixtures::addRigidBody(controlMover);

    const auto controlStatic = fixtures::addObject(scene, "ControlStatic", { 301, 0, 0 });
    fixtures::addBoxCollider(controlStatic);

    CollisionSystem collisionSystem;
    collisionSystem.fixedUpdate(*scene.objectManager, dt);

    CollisionRun run;
    run.moverPosition = fixtures::positionOf(mover);

    for (const auto& pair : collisionSystem.getCollisionEnters())
    {
      run.terrainPaired = run.terrainPaired || pair.a == terrainObject->getUUID() || pair.b == terrainObject->getUUID();
      run.controlPaired = run.controlPaired ||
        CollisionPair::make(controlMover->getUUID(), controlStatic->getUUID()) == pair;
    }

    return run;
  }
}

TEST(TerrainComponentTest, ABodyInsideATerrainsBoundsProducesNoContact)
{
  for (const bool enabled : { true, false })
  {
    const auto run = runCollision(enabled);

    EXPECT_TRUE(run.controlPaired);
    EXPECT_FALSE(run.terrainPaired);
    fixtures::expectNear(run.moverPosition, { 30, 30, 30 }, tolerance);
  }
}

TEST(TerrainComponentTest, ATerrainIsNeverAPairAcrossTicksWhileOrdinaryPairsAre)
{
  const auto scene = makeScene();

  const auto [terrainObject, comp] = addTerrain(scene);
  const auto volume = volumeWithChunk({ 0, 0, 0 });
  comp->setVolume(volume);

  const auto inside = fixtures::addObject(scene, "Inside", { 30, 30, 30 });
  fixtures::addBoxCollider(inside);
  fixtures::addRigidBody(inside);

  const auto controlMover = fixtures::addObject(scene, "ControlMover", { 300, 0, 0 });
  fixtures::addBoxCollider(controlMover);
  fixtures::addRigidBody(controlMover);

  // A trigger takes no response, so the pair persists and a stay is possible on the second tick.
  const auto controlStatic = fixtures::addObject(scene, "ControlStatic", { 300.5f, 0, 0 });
  fixtures::addBoxCollider(controlStatic)->setIsTrigger(true);

  const auto controlPair = CollisionPair::make(controlMover->getUUID(), controlStatic->getUUID());
  const auto touchesTerrain = [&](const std::vector<CollisionPair>& pairs)
  {
    return std::ranges::any_of(pairs, [&](const CollisionPair& pair)
    {
      return pair.a == terrainObject->getUUID() || pair.b == terrainObject->getUUID();
    });
  };
  const auto contains = [](const std::vector<CollisionPair>& pairs, const CollisionPair& pair)
  {
    return std::ranges::find(pairs, pair) != pairs.end();
  };

  CollisionSystem collisionSystem;

  collisionSystem.fixedUpdate(*scene.objectManager, dt);
  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), controlPair));
  EXPECT_FALSE(touchesTerrain(collisionSystem.getCollisionEnters()));
  EXPECT_FALSE(touchesTerrain(collisionSystem.getCollisionStays()));
  EXPECT_FALSE(touchesTerrain(collisionSystem.getCollisionExits()));

  collisionSystem.fixedUpdate(*scene.objectManager, dt);
  EXPECT_TRUE(contains(collisionSystem.getCollisionStays(), controlPair));
  EXPECT_FALSE(touchesTerrain(collisionSystem.getCollisionEnters()));
  EXPECT_FALSE(touchesTerrain(collisionSystem.getCollisionStays()));
  EXPECT_FALSE(touchesTerrain(collisionSystem.getCollisionExits()));
}

TEST(TerrainComponentTest, ATerrainObjectWithARigidBodyDoesNotBreakTheTick)
{
  const auto scene = makeScene();

  const auto [terrainObject, comp] = addTerrain(scene);
  const auto volume = volumeWithChunk({ 0, 0, 0 });
  comp->setVolume(volume);
  fixtures::addRigidBody(terrainObject);

  const auto mover = fixtures::addObject(scene, "Mover", { 300, 0, 0 });
  fixtures::addBoxCollider(mover);
  fixtures::addRigidBody(mover);

  const auto other = fixtures::addObject(scene, "Other", { 300.5f, 0, 0 });
  fixtures::addBoxCollider(other);

  CollisionSystem collisionSystem;
  EXPECT_NO_THROW(collisionSystem.fixedUpdate(*scene.objectManager, dt));
  EXPECT_NO_THROW(collisionSystem.fixedUpdate(*scene.objectManager, dt));

  // The ordinary pair beside it still collides, so the tick ran rather than skipping everything.
  EXPECT_FALSE(collisionSystem.getCollisionEnters().empty() && collisionSystem.getCollisionStays().empty());
}

namespace {
  bool castRay(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, uuids::uuid& object,
               float& distance)
  {
    glm::vec3 point;
    glm::vec3 normal;

    return SceneQueries::raycast(*scene.objectManager, origin, direction, 200.0f, allLayers, {}, object, point,
                                 normal, distance);
  }
}

TEST(TerrainComponentTest, QueriesSkipATerrainAndStillHitOrdinaryColliders)
{
  const auto scene = makeScene();
  const auto [terrainObject, comp] = addTerrain(scene);
  const auto volume = volumeWithChunk({ 0, 0, 0 });
  comp->setVolume(volume);

  uuids::uuid object;
  float distance = 0.0f;
  EXPECT_FALSE(castRay(scene, { 30, 30, -10 }, { 0, 0, 1 }, object, distance));

  std::vector<uuids::uuid> overlaps;
  SceneQueries::overlapSphere(*scene.objectManager, { 30, 30, 30 }, 1.0f, allLayers, {}, overlaps);
  EXPECT_TRUE(overlaps.empty());

  // The same queries find a box placed behind and inside the terrain's bounds.
  const auto box = fixtures::addObject(scene, "Box", { 30, 30, 80 });
  fixtures::addBoxCollider(box);
  const auto inside = fixtures::addObject(scene, "Inside", { 30, 30, 30 });
  fixtures::addBoxCollider(inside);

  EXPECT_TRUE(castRay(scene, { 30, 30, -10 }, { 0, 0, 1 }, object, distance));
  EXPECT_EQ(object, inside->getUUID());
  EXPECT_NEAR(distance, 39.0f, tolerance);

  SceneQueries::overlapSphere(*scene.objectManager, { 30, 30, 30 }, 1.0f, allLayers, {}, overlaps);
  ASSERT_EQ(overlaps.size(), 1u);
  EXPECT_EQ(overlaps.front(), inside->getUUID());
}
