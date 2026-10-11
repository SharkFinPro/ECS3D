#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "TestScene.h"
#include "WireTruncation.h"
#include "WireTypes.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/ConvexHull.h"
#include "objects/components/collisions/ConvexMeshCollider.h"

#include <Protocol.h>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
  using fixtures::addChildObject;
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::transformOf;

  // Looser than the fixture default: these coordinates come out of a matrix.
  void expectNear(const glm::vec3& actual, const glm::vec3& expected)
  {
    fixtures::expectNear(actual, expected, 1e-4f);
  }

  void expectNear(const char* what, const glm::vec3& actual, const glm::vec3& expected, const float tolerance = 1e-4f)
  {
    fixtures::expectNear(what, actual, expected, tolerance);
  }

  const uuids::uuid sourceModel = uuids::uuid::from_string("11111111-2222-3333-4444-555555555555").value();

  // Apex above a base triangle: a hull that is not the default cube.
  ConvexHull tetrahedron()
  {
    const std::vector<glm::vec3> points{ { -1, -1, -1 }, { 1, -1, -1 }, { 0, -1, 1 }, { 0, 1, 0 } };

    return *buildConvexHull(points);
  }

  std::shared_ptr<ConvexMeshCollider> customCollider()
  {
    auto collider = std::make_shared<ConvexMeshCollider>();
    EXPECT_TRUE(collider->setHull(tetrahedron()));
    collider->setSourceModel(sourceModel);
    collider->setPosition({ 1.0f, 2.0f, 3.0f });
    collider->setRotation({ 10.0f, 20.0f, 30.0f });
    collider->setScale({ 0.5f, 1.5f, 2.5f });
    collider->setRenderCollider(true);
    collider->setIsTrigger(true);
    collider->setLayer(5);
    collider->setMask(0xF0u);

    return collider;
  }

  void expectSameCollider(ConvexMeshCollider& actual, ConvexMeshCollider& expected)
  {
    EXPECT_EQ(actual.getHull().vertices, expected.getHull().vertices);
    EXPECT_EQ(actual.getHull().indices, expected.getHull().indices);
    EXPECT_EQ(actual.getSourceModel(), expected.getSourceModel());
    EXPECT_EQ(actual.getLocalPosition(), expected.getLocalPosition());
    EXPECT_EQ(actual.getLocalRotation(), expected.getLocalRotation());
    EXPECT_EQ(actual.getLocalScale(), expected.getLocalScale());
    EXPECT_EQ(actual.getRenderCollider(), expected.getRenderCollider());
    EXPECT_EQ(actual.isTrigger(), expected.isTrigger());
    EXPECT_EQ(actual.getLayer(), expected.getLayer());
    EXPECT_EQ(actual.getMask(), expected.getMask());
  }

  net::Message packed(const ConvexMeshCollider& collider)
  {
    net::Message message(net::MessageType::undefined);
    collider.pack(message);

    return message;
  }

  // Reads the discriminator pack writes first, then unpacks, as Object::unpack does.
  void unpackInto(ConvexMeshCollider& target, const net::Message& message)
  {
    net::MessageReader reader(message);
    static_cast<void>(reader.read<ComponentType>());
    target.unpack(reader);
  }

  // The fields before the hull, in pack order, for hand-built hostile payloads.
  net::Message headerOnly()
  {
    net::Message message(net::MessageType::undefined);
    message.write(ComponentType::SubComponentType_convexMeshCollider);
    message.write(false);
    message.write(glm::vec3(0.0f));
    message.write(glm::vec3(1.0f));
    message.write(glm::vec3(0.0f));
    message.write(uuids::uuid{});

    return message;
  }

  void writeUnitCubeVertices(net::Message& message)
  {
    const auto cube = makeUnitCubeHull();
    message.write(static_cast<uint32_t>(cube.vertices.size()));

    for (const auto& vertex : cube.vertices)
    {
      message.write(vertex);
    }
  }

  void writeFlags(net::Message& message)
  {
    message.write(false);
    message.write(uint32_t{ 0 });
    message.write(uint32_t{ 0xFFFFFFFFu });
  }

  void expectUnchangedDefault(ConvexMeshCollider& collider)
  {
    ConvexMeshCollider untouched;
    EXPECT_EQ(collider.getHull().vertices, untouched.getHull().vertices);
    EXPECT_EQ(collider.getHull().indices, untouched.getHull().indices);
    EXPECT_TRUE(collider.getSourceModel().is_nil());
    EXPECT_EQ(collider.getLocalPosition(), glm::vec3(0.0f));
    EXPECT_EQ(collider.getLocalScale(), glm::vec3(1.0f));
  }

  struct Placed
  {
    std::shared_ptr<Object> object;
    std::shared_ptr<ConvexMeshCollider> hull;
  };

  Placed placeHull(const fixtures::Scene& scene, const glm::vec3& position = glm::vec3(0.0f))
  {
    auto object = addObject(scene, "Hull", position);
    auto hull = std::make_shared<ConvexMeshCollider>();
    object->addComponent(hull);

    return { object, hull };
  }
}

TEST(ConvexMeshCollider, StartsAsTheUnitCubeSoItActsLikeADefaultBox)
{
  ConvexMeshCollider collider;

  EXPECT_EQ(collider.getHull().vertices.size(), 8u);
  EXPECT_EQ(collider.getHull().indices.size(), 36u);
  EXPECT_TRUE(isValidConvexHull(collider.getHull()));
  EXPECT_TRUE(collider.getSourceModel().is_nil());
  EXPECT_EQ(collider.getLocalPosition(), glm::vec3(0.0f));
  EXPECT_EQ(collider.getLocalRotation(), glm::vec3(0.0f));
  EXPECT_EQ(collider.getLocalScale(), glm::vec3(1.0f));
  EXPECT_EQ(collider.getColliderType(), ColliderType::convexMeshCollider);
  EXPECT_EQ(collider.getSubType(), ComponentType::SubComponentType_convexMeshCollider);
  EXPECT_EQ(collider.getType(), ComponentType::collider);
}

TEST(ConvexMeshCollider, IsRegisteredUnderItsKeyAndNamedInTheComponentMaps)
{
  const auto scene = makeScene();

  const auto created = scene.componentRegistry->create("ConvexMesh");
  ASSERT_NE(created, nullptr);
  EXPECT_NE(std::dynamic_pointer_cast<ConvexMeshCollider>(created), nullptr);

  EXPECT_EQ(componentTypeToString.at(ComponentType::SubComponentType_convexMeshCollider), "Convex Mesh Collider");
  EXPECT_EQ(subComponentTypeToParent.at(ComponentType::SubComponentType_convexMeshCollider),
            ComponentType::collider);
  EXPECT_EQ(componentTypeToRegistryKey.at(ComponentType::SubComponentType_convexMeshCollider), "ConvexMesh");

  // Appended just above camera, so every existing discriminator keeps its wire value.
  EXPECT_EQ(static_cast<int>(ComponentType::SubComponentType_convexMeshCollider),
            static_cast<int>(ComponentType::camera) + 1);
  EXPECT_EQ(static_cast<int>(ComponentType::count),
            static_cast<int>(ComponentType::SubComponentType_convexMeshCollider) + 1);
}

TEST(ConvexMeshCollider, KeepsItsHullSourceOffsetsAndFlagsThroughJson)
{
  const auto source = customCollider();

  const auto data = source->serialize();
  EXPECT_EQ(data.at("type"), "Collider");
  EXPECT_EQ(data.at("subType"), "ConvexMesh");
  EXPECT_EQ(data.at("sourceModel"), uuids::to_string(sourceModel));
  EXPECT_EQ(data.at("vertices").size(), source->getHull().vertices.size() * 3);
  EXPECT_EQ(data.at("indices").size(), source->getHull().indices.size());

  // The control: a collider that is not the source differs, so the comparison can fail.
  auto loaded = std::make_shared<ConvexMeshCollider>();
  EXPECT_NE(loaded->getHull().vertices, source->getHull().vertices);

  loaded->loadFromJSON(data);

  expectSameCollider(*loaded, *source);
}

TEST(ConvexMeshCollider, ANilSourceModelSavesAsAnEmptyStringAndLoadsBackNil)
{
  ConvexMeshCollider source;

  const auto data = source.serialize();
  EXPECT_EQ(data.at("sourceModel"), "");

  ConvexMeshCollider loaded;
  loaded.setSourceModel(sourceModel);
  loaded.loadFromJSON(data);

  EXPECT_TRUE(loaded.getSourceModel().is_nil());

  auto withoutKey = data;
  withoutKey.erase("sourceModel");
  loaded.setSourceModel(sourceModel);
  loaded.loadFromJSON(withoutKey);

  EXPECT_TRUE(loaded.getSourceModel().is_nil());
}

TEST(ConvexMeshCollider, JsonWithoutTheOptionalFlagsLoadsWithTheirDefaults)
{
  auto data = ConvexMeshCollider().serialize();
  data.erase("renderCollider");
  data.erase("isTrigger");
  data.erase("layer");
  data.erase("mask");

  ConvexMeshCollider loaded;
  loaded.setIsTrigger(true);
  loaded.setLayer(7);
  loaded.loadFromJSON(data);

  EXPECT_FALSE(loaded.getRenderCollider());
  EXPECT_FALSE(loaded.isTrigger());
  EXPECT_EQ(loaded.getLayer(), 0u);
  EXPECT_EQ(loaded.getMask(), 0xFFFFFFFFu);
}

TEST(ConvexMeshCollider, JsonWithAnInvalidHullThrowsAndLeavesTheColliderAlone)
{
  const auto valid = ConvexMeshCollider().serialize();

  // The control: the unmodified blob loads.
  ConvexMeshCollider accepts;
  EXPECT_NO_THROW(accepts.loadFromJSON(valid));

  auto outOfRange = valid;
  outOfRange["indices"][3] = 99;

  auto tooFewVertices = valid;
  tooFewVertices["vertices"] = nlohmann::json::array({ 0, 0, 0, 1, 0, 0, 0, 1, 0 });
  tooFewVertices["indices"] = nlohmann::json::array({ 0, 1, 2 });

  auto ragged = valid;
  ragged["vertices"].push_back(1.0f);

  auto notANumber = valid;
  notANumber["vertices"][4] = nullptr;

  auto noIndices = valid;
  noIndices["indices"] = nlohmann::json::array();

  auto missingVertices = valid;
  missingVertices.erase("vertices");

  for (const auto& broken : { outOfRange, tooFewVertices, ragged, notANumber, noIndices, missingVertices })
  {
    ConvexMeshCollider target;
    target.setPosition({ 4.0f, 5.0f, 6.0f });

    EXPECT_ANY_THROW(target.loadFromJSON(broken));
    EXPECT_EQ(target.getLocalPosition(), glm::vec3(4.0f, 5.0f, 6.0f));
    EXPECT_EQ(target.getHull().vertices.size(), 8u);
  }
}

TEST(ConvexMeshCollider, KeepsItsHullSourceOffsetsAndFlagsOverTheWire)
{
  const auto source = customCollider();
  const auto message = packed(*source);

  net::MessageReader discriminator(message);
  EXPECT_EQ(discriminator.read<ComponentType>(), ComponentType::SubComponentType_convexMeshCollider);
  EXPECT_EQ(source->getPackedType(), ComponentType::SubComponentType_convexMeshCollider);

  ConvexMeshCollider loaded;
  unpackInto(loaded, message);

  expectSameCollider(loaded, *source);
}

TEST(ConvexMeshCollider, RefusesEveryTruncatedPayloadAndKeepsWhatItHad)
{
  const auto message = packed(*customCollider());

  wiretest::forEachProperPrefix(message, [](const net::Message& prefix, std::size_t) {
    ConvexMeshCollider target;

    EXPECT_ANY_THROW(unpackInto(target, prefix));
    expectUnchangedDefault(target);
  });

  // The control: the whole message is accepted.
  ConvexMeshCollider whole;
  EXPECT_NO_THROW(unpackInto(whole, message));
  EXPECT_EQ(whole.getHull().vertices.size(), 4u);
}

TEST(ConvexMeshCollider, RefusesAnOversizedVertexCountWithoutAllocatingForIt)
{
  auto message = headerOnly();
  message.write(std::numeric_limits<uint32_t>::max());

  ConvexMeshCollider target;

  // runtime_error is the bound check; a count that was trusted would be a bad_alloc or length_error.
  EXPECT_THROW(unpackInto(target, message), std::runtime_error);
  expectUnchangedDefault(target);

  auto justOver = headerOnly();
  justOver.write(static_cast<uint32_t>(maxConvexHullVertices + 1));
  EXPECT_THROW(unpackInto(target, justOver), std::runtime_error);
  expectUnchangedDefault(target);
}

TEST(ConvexMeshCollider, RefusesAnOversizedIndexCountWithoutAllocatingForIt)
{
  auto message = headerOnly();
  writeUnitCubeVertices(message);
  message.write(std::numeric_limits<uint32_t>::max());

  ConvexMeshCollider target;

  EXPECT_THROW(unpackInto(target, message), std::runtime_error);
  expectUnchangedDefault(target);

  auto justOver = headerOnly();
  writeUnitCubeVertices(justOver);
  justOver.write(static_cast<uint32_t>(maxConvexHullIndices + 1));
  EXPECT_THROW(unpackInto(target, justOver), std::runtime_error);
  expectUnchangedDefault(target);
}

TEST(ConvexMeshCollider, RefusesAWireHullWhoseIndexNamesNoVertex)
{
  const auto cube = makeUnitCubeHull();

  auto build = [&cube](const uint32_t badIndex) {
    auto message = headerOnly();
    writeUnitCubeVertices(message);
    message.write(static_cast<uint32_t>(cube.indices.size()));

    for (size_t i = 0; i < cube.indices.size(); ++i)
    {
      message.write(i == 4 ? badIndex : cube.indices[i]);
    }

    writeFlags(message);

    return message;
  };

  ConvexMeshCollider target;
  EXPECT_THROW(unpackInto(target, build(8)), std::runtime_error);
  expectUnchangedDefault(target);

  // The control: the same payload with that index left alone is accepted.
  EXPECT_NO_THROW(unpackInto(target, build(cube.indices[4])));
}

TEST(ConvexMeshCollider, SetHullTakesAValidHullAndRefusesEveryInvalidOne)
{
  ConvexMeshCollider collider;
  const auto original = collider.getHull();

  auto fewVertices = makeUnitCubeHull();
  fewVertices.vertices.resize(3);
  fewVertices.indices = { 0, 1, 2 };

  auto noTriangles = makeUnitCubeHull();
  noTriangles.indices.clear();

  auto ragged = makeUnitCubeHull();
  ragged.indices.pop_back();

  auto outOfRange = makeUnitCubeHull();
  outOfRange.indices[0] = 8;

  auto nonFinite = makeUnitCubeHull();
  nonFinite.vertices[0].x = std::numeric_limits<float>::infinity();

  auto tooMany = makeUnitCubeHull();
  tooMany.vertices.resize(maxConvexHullVertices + 1, glm::vec3(1.0f));

  for (auto* broken : { &fewVertices, &noTriangles, &ragged, &outOfRange, &nonFinite, &tooMany })
  {
    EXPECT_FALSE(collider.setHull(*broken));
    EXPECT_EQ(collider.getHull().vertices, original.vertices);
    EXPECT_EQ(collider.getHull().indices, original.indices);
  }

  EXPECT_TRUE(collider.setHull(tetrahedron()));
  EXPECT_EQ(collider.getHull().vertices.size(), 4u);
}

TEST(ConvexMeshCollider, IgnoresNonFiniteOffsets)
{
  ConvexMeshCollider collider;
  collider.setPosition({ 1.0f, 2.0f, 3.0f });
  collider.setRotation({ 4.0f, 5.0f, 6.0f });
  collider.setScale({ 7.0f, 8.0f, 9.0f });

  const float nan = std::numeric_limits<float>::quiet_NaN();
  collider.setPosition({ nan, 0.0f, 0.0f });
  collider.setRotation({ 0.0f, nan, 0.0f });
  collider.setScale({ 0.0f, 0.0f, std::numeric_limits<float>::infinity() });

  EXPECT_EQ(collider.getLocalPosition(), glm::vec3(1.0f, 2.0f, 3.0f));
  EXPECT_EQ(collider.getLocalRotation(), glm::vec3(4.0f, 5.0f, 6.0f));
  EXPECT_EQ(collider.getLocalScale(), glm::vec3(7.0f, 8.0f, 9.0f));
}

TEST(ConvexMeshCollider, WorldVerticesFollowTheTransformTheOffsetsAndTheHull)
{
  const auto scene = makeScene();
  const auto [object, hull] = placeHull(scene, { 10.0f, 0.0f, 0.0f });

  // The cube corner furthest toward +x +y +z.
  expectNear(hull->findFurthestPoint({ 1.0f, 1.0f, 1.0f }), { 11.0f, 1.0f, 1.0f });

  transformOf(object)->setPosition({ 20.0f, 0.0f, 0.0f });
  expectNear(hull->findFurthestPoint({ 1.0f, 1.0f, 1.0f }), { 21.0f, 1.0f, 1.0f });

  hull->setPosition({ 0.0f, 5.0f, 0.0f });
  expectNear(hull->findFurthestPoint({ 1.0f, 1.0f, 1.0f }), { 21.0f, 6.0f, 1.0f });

  hull->setScale({ 2.0f, 1.0f, 1.0f });
  expectNear(hull->findFurthestPoint({ 1.0f, 1.0f, 1.0f }), { 22.0f, 6.0f, 1.0f });

  ASSERT_TRUE(hull->setHull(tetrahedron()));
  EXPECT_EQ(hull->getWorldVertices().size(), 4u);
  expectNear(hull->findFurthestPoint({ 0.0f, 1.0f, 0.0f }), { 20.0f, 6.0f, 0.0f });
}

TEST(ConvexMeshCollider, TheBoundingBoxFollowsChangesToTheColliderItself)
{
  const auto scene = makeScene();
  const auto [object, hull] = placeHull(scene);

  EXPECT_FLOAT_EQ(hull->getBoundingBox().maxX, 1.0f);

  hull->setScale({ 3.0f, 1.0f, 1.0f });
  EXPECT_FLOAT_EQ(hull->getBoundingBox().maxX, 3.0f);

  hull->setPosition({ 4.0f, 0.0f, 0.0f });
  EXPECT_FLOAT_EQ(hull->getBoundingBox().maxX, 7.0f);
  EXPECT_FLOAT_EQ(hull->getBoundingBox().minX, 1.0f);

  ASSERT_TRUE(hull->setHull(tetrahedron()));
  EXPECT_FLOAT_EQ(hull->getBoundingBox().maxY, 1.0f);
  EXPECT_FLOAT_EQ(hull->getBoundingBox().minY, -1.0f);
  EXPECT_FLOAT_EQ(hull->getBoundingBox().maxX, 4.0f + 3.0f);
}

TEST(ConvexMeshCollider, TheEdgeListIsCachedUntilTheHullChanges)
{
  ConvexMeshCollider collider;

  EXPECT_EQ(collider.getEdges().size(), 12u);
  EXPECT_EQ(&collider.getEdges(), &collider.getEdges());

  ASSERT_TRUE(collider.setHull(tetrahedron()));
  EXPECT_EQ(collider.getEdges().size(), 6u);
}

namespace {
  struct Pair
  {
    std::shared_ptr<BoxCollider> box;
    std::shared_ptr<ConvexMeshCollider> hull;
    std::shared_ptr<Object> boxObject;
    std::shared_ptr<Object> hullObject;
  };

  void configure(const std::shared_ptr<Object>& object)
  {
    const auto transform = transformOf(object);
    transform->setPosition({ 1.0f, 2.0f, 3.0f });
    transform->setRotation({ 10.0f, 20.0f, 30.0f });
    transform->setScale({ 1.5f, 2.0f, 0.75f });
  }

  void expectSamePlacement(Pair& pair)
  {
    expectNear("position", pair.hull->getPosition(), pair.box->getPosition());
    expectNear("scale", pair.hull->getScale(), pair.box->getScale());
    expectNear("rotation", pair.hull->getRotation(), pair.box->getRotation(), 1e-3f);

    const std::array<glm::vec3, 6> directions{ {
      { 0.3f, 0.5f, 0.8f }, { -0.7f, 0.2f, -0.4f }, { 0.1f, -0.9f, 0.35f },
      { -0.45f, -0.3f, 0.6f }, { 0.9f, 0.15f, -0.2f }, { -0.1f, 0.7f, 0.55f }
    } };

    for (const auto& direction : directions)
    {
      expectNear("furthest", pair.hull->findFurthestPoint(direction), pair.box->findFurthestPoint(direction),
                 1e-4f);
    }

    const auto& boxBounds = pair.box->getBoundingBox();
    const auto& hullBounds = pair.hull->getBoundingBox();
    EXPECT_NEAR(hullBounds.minX, boxBounds.minX, 1e-4f);
    EXPECT_NEAR(hullBounds.maxX, boxBounds.maxX, 1e-4f);
    EXPECT_NEAR(hullBounds.minY, boxBounds.minY, 1e-4f);
    EXPECT_NEAR(hullBounds.maxY, boxBounds.maxY, 1e-4f);
    EXPECT_NEAR(hullBounds.minZ, boxBounds.minZ, 1e-4f);
    EXPECT_NEAR(hullBounds.maxZ, boxBounds.maxZ, 1e-4f);
  }

  void applyOffsets(Pair& pair)
  {
    pair.box->setPosition({ 0.5f, -0.25f, 0.2f });
    pair.hull->setPosition({ 0.5f, -0.25f, 0.2f });
    pair.box->setRotation({ 5.0f, 15.0f, -25.0f });
    pair.hull->setRotation({ 5.0f, 15.0f, -25.0f });
    pair.box->setScale({ 1.0f, 0.5f, 2.0f });
    pair.hull->setScale({ 1.0f, 0.5f, 2.0f });
  }
}

TEST(ConvexMeshCollider, TheDefaultCubeHullPlacesItselfLikeABoxWithTheSameOffsets)
{
  const auto scene = makeScene();

  Pair pair;
  pair.boxObject = addObject(scene, "Box");
  pair.hullObject = addObject(scene, "Hull");
  pair.box = fixtures::addBoxCollider(pair.boxObject);
  pair.hull = std::make_shared<ConvexMeshCollider>();
  pair.hullObject->addComponent(pair.hull);

  configure(pair.boxObject);
  configure(pair.hullObject);
  expectSamePlacement(pair);

  applyOffsets(pair);
  expectSamePlacement(pair);
}

TEST(ConvexMeshCollider, TheDefaultCubeHullPlacesItselfLikeABoxUnderARotatedScaledParent)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent", { 4.0f, -1.0f, 2.0f });
  transformOf(parent)->setRotation({ 0.0f, 30.0f, 15.0f });
  transformOf(parent)->setScale({ 2.0f, 1.0f, 1.5f });

  Pair pair;
  pair.boxObject = addChildObject(scene, "Box", parent);
  pair.hullObject = addChildObject(scene, "Hull", parent);
  pair.box = fixtures::addBoxCollider(pair.boxObject);
  pair.hull = std::make_shared<ConvexMeshCollider>();
  pair.hullObject->addComponent(pair.hull);

  configure(pair.boxObject);
  configure(pair.hullObject);
  applyOffsets(pair);
  expectSamePlacement(pair);

  // Moving the ancestor moves both, and the cache notices.
  transformOf(parent)->setPosition({ -6.0f, 3.0f, 1.0f });
  expectSamePlacement(pair);
}

TEST(ConvexMeshCollider, SurvivesAnObjectTreeRoundTripThroughJsonAndTheWire)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Holder", { 1.0f, 2.0f, 3.0f });
  const auto source = customCollider();
  object->addComponent(source);

  const auto saved = scene.objectManager->serialize();

  auto fromJson = makeScene();
  fromJson.objectManager->restoreFromJSON(saved.at("objects"));
  const auto jsonHull = fromJson.objectManager->getObjectByUUID(object->getUUID())
                          ->getComponent<ConvexMeshCollider>(ComponentType::collider);
  ASSERT_NE(jsonHull, nullptr);
  expectSameCollider(*jsonHull, *source);

  net::Message snapshot(net::MessageType::snapshot);
  scene.objectManager->pack(snapshot);

  auto fromWire = makeScene();
  net::MessageReader reader(snapshot);
  fromWire.objectManager->unpack(reader);
  const auto wireHull = fromWire.objectManager->getObjectByUUID(object->getUUID())
                          ->getComponent<ConvexMeshCollider>(ComponentType::collider);
  ASSERT_NE(wireHull, nullptr);
  expectSameCollider(*wireHull, *source);
}

TEST(ConvexMeshCollider, ArrivesAsAComponentEditOnlyOnAHullAndNotOnAnotherShape)
{
  const auto scene = makeScene();
  const auto onHull = addObject(scene, "OnHull");
  const auto hull = std::make_shared<ConvexMeshCollider>();
  onHull->addComponent(hull);

  const auto edit = replication::buildComponentEdit(onHull->getUUID(), customCollider());

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit), replication::ComponentEditResult::applied);
  EXPECT_EQ(hull->getHull().vertices.size(), 4u);
  EXPECT_EQ(hull->getSourceModel(), sourceModel);

  // A hull body read as a box would corrupt it, so the shape in the slot has to match.
  const auto onBox = addObject(scene, "OnBox");
  const auto box = fixtures::addBoxCollider(onBox);
  box->setScale({ 4, 5, 6 });

  const auto misdirected = replication::buildComponentEdit(onBox->getUUID(), customCollider());

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, misdirected),
            replication::ComponentEditResult::unknownComponent);
  EXPECT_EQ(box->getLocalScale(), glm::vec3(4, 5, 6));
}
