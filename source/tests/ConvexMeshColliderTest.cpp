#include <gtest/gtest.h>

#include "ConvexMeshFixtures.h"
#include "TestPrinters.h"
#include "TestScene.h"
#include "WireTruncation.h"
#include "WireTypes.h"
#include "ComponentRegistry.h"
#include "objects/components/Transform.h"
#include "objects/Object.h"
#include "objects/components/Component.h"
#include "objects/components/collisions/ConvexHull.h"
#include "objects/components/collisions/ConvexHullValidation.h"
#include "objects/components/collisions/ConvexMeshCollider.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
  using convexFixtures::customCollider;
  using convexFixtures::expectSameCollider;
  using convexFixtures::sourceModel;
  using convexFixtures::tetrahedron;
  using convexFixtures::expectNear;
  using fixtures::makeScene;

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
  net::Message headerOnly(const glm::vec3& position = glm::vec3(0.0f))
  {
    net::Message message(net::MessageType::undefined);
    message.write(ComponentType::SubComponentType_convexMeshCollider);
    message.write(false);
    message.write(position);
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

  // MessageReader underflow is also a runtime_error, so the text says which check threw.
  void expectRefusedWith(ConvexMeshCollider& target, const net::Message& message, const std::string& text)
  {
    try
    {
      unpackInto(target, message);
      ADD_FAILURE() << "expected a refusal containing: " << text;
    }
    catch (const std::runtime_error& error)
    {
      EXPECT_NE(std::string(error.what()).find(text), std::string::npos) << error.what();
    }
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

  // Appended just above the capsule, so every existing discriminator keeps its wire value.
  EXPECT_EQ(static_cast<int>(ComponentType::SubComponentType_convexMeshCollider),
            static_cast<int>(ComponentType::SubComponentType_capsuleCollider) + 1);
  EXPECT_EQ(static_cast<int>(ComponentType::count),
            static_cast<int>(ComponentType::SubComponentType_convexMeshCollider) + 1);
}

TEST(ConvexMeshCollider, KeepsItsHullSourceOffsetsAndFlagsThroughJsonAndTheWire)
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

  const auto message = packed(*source);
  net::MessageReader discriminator(message);
  EXPECT_EQ(discriminator.read<ComponentType>(), ComponentType::SubComponentType_convexMeshCollider);
  EXPECT_EQ(source->getPackedType(), ComponentType::SubComponentType_convexMeshCollider);

  ConvexMeshCollider fromWire;
  unpackInto(fromWire, message);
  expectSameCollider(fromWire, *source);
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

TEST(ConvexMeshCollider, RefusesAnOversizedCountWithoutAllocatingForIt)
{
  const std::string boundCheck = "count over the limit";
  ConvexMeshCollider target;

  // The control: a count within the limit but with no data behind it runs out of payload, not the bound.
  auto withinLimit = headerOnly();
  withinLimit.write(uint32_t{ 1 });
  net::MessageReader reader(withinLimit);
  static_cast<void>(reader.read<ComponentType>());
  try
  {
    target.unpack(reader);
    ADD_FAILURE() << "expected the truncated payload to throw";
  }
  catch (const std::runtime_error& error)
  {
    EXPECT_EQ(std::string(error.what()).find(boundCheck), std::string::npos) << error.what();
  }

  for (const uint32_t count : { std::numeric_limits<uint32_t>::max(), static_cast<uint32_t>(maxConvexHullVertices + 1) })
  {
    auto message = headerOnly();
    message.write(count);
    expectRefusedWith(target, message, boundCheck);
    expectUnchangedDefault(target);
  }

  for (const uint32_t count : { std::numeric_limits<uint32_t>::max(), static_cast<uint32_t>(maxConvexHullIndices + 1) })
  {
    auto message = headerOnly();
    writeUnitCubeVertices(message);
    message.write(count);
    expectRefusedWith(target, message, boundCheck);
    expectUnchangedDefault(target);
  }
}

TEST(ConvexMeshCollider, RefusesANonFiniteWireOffsetAndKeepsWhatItHad)
{
  const auto cube = makeUnitCubeHull();

  auto build = [&cube](const glm::vec3& position) {
    auto message = headerOnly(position);
    writeUnitCubeVertices(message);
    message.write(static_cast<uint32_t>(cube.indices.size()));

    for (const uint32_t index : cube.indices)
    {
      message.write(index);
    }

    writeFlags(message);

    return message;
  };

  // The control: the same payload with a finite offset lands.
  ConvexMeshCollider accepts;
  EXPECT_NO_THROW(unpackInto(accepts, build({ 1.0f, 2.0f, 3.0f })));
  EXPECT_EQ(accepts.getLocalPosition(), glm::vec3(1.0f, 2.0f, 3.0f));

  ConvexMeshCollider target;
  expectRefusedWith(target, build({ std::numeric_limits<float>::infinity(), 0.0f, 0.0f }), "non-finite");
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

TEST(ConvexMeshCollider, JsonWithAMalformedOffsetLeavesTheWholeColliderAlone)
{
  auto data = customCollider()->serialize();
  data["position"] = nlohmann::json::array({ 1.0f, "x", 3.0f });

  ConvexMeshCollider target;
  target.setPosition({ 4.0f, 5.0f, 6.0f });

  // The hull and source model in the blob are valid; only the offset is not, and none of it may land.
  EXPECT_ANY_THROW(target.loadFromJSON(data));
  EXPECT_EQ(target.getLocalPosition(), glm::vec3(4.0f, 5.0f, 6.0f));
  EXPECT_EQ(target.getHull().vertices.size(), 8u);
  EXPECT_TRUE(target.getSourceModel().is_nil());
  EXPECT_FALSE(target.isTrigger());
  EXPECT_EQ(target.getLayer(), 0u);
}

TEST(ConvexMeshCollider, JsonWithANonFiniteOffsetLeavesTheWholeColliderAlone)
{
  const auto valid = customCollider()->serialize();

  // 1e39 is a finite double that reads back as an infinite float.
  for (const char* key : { "position", "rotation", "scale" })
  {
    auto data = valid;
    data[key] = nlohmann::json::array({ 1.0f, 1e39, 3.0f });

    ConvexMeshCollider target;
    target.setPosition({ 4.0f, 5.0f, 6.0f });

    EXPECT_ANY_THROW(target.loadFromJSON(data)) << key;
    EXPECT_EQ(target.getLocalPosition(), glm::vec3(4.0f, 5.0f, 6.0f)) << key;
    EXPECT_EQ(target.getLocalScale(), glm::vec3(1.0f)) << key;
    EXPECT_EQ(target.getHull().vertices.size(), 8u) << key;
  }

  ConvexMeshCollider accepts;
  EXPECT_NO_THROW(accepts.loadFromJSON(valid));
}

TEST(ConvexMeshCollider, WorldVerticesFollowTheTransformTheOffsetsAndTheHull)
{
  const auto scene = makeScene();
  const auto object = fixtures::addObject(scene, "Hull", { 10.0f, 0.0f, 0.0f });
  const auto hull = std::make_shared<ConvexMeshCollider>();
  object->addComponent(hull);

  // The cube corner furthest toward +x +y +z.
  expectNear(hull->findFurthestPoint({ 1.0f, 1.0f, 1.0f }), { 11.0f, 1.0f, 1.0f });

  fixtures::transformOf(object)->setPosition({ 20.0f, 0.0f, 0.0f });
  hull->setPosition({ 0.0f, 5.0f, 0.0f });
  hull->setScale({ 2.0f, 1.0f, 1.0f });
  expectNear(hull->findFurthestPoint({ 1.0f, 1.0f, 1.0f }), { 22.0f, 6.0f, 1.0f });
  EXPECT_FLOAT_EQ(hull->getBoundingBox().maxX, 22.0f);

  ASSERT_TRUE(hull->setHull(tetrahedron()));
  EXPECT_EQ(hull->getWorldVertices().size(), 4u);
  expectNear(hull->findFurthestPoint({ 0.0f, 1.0f, 0.0f }), { 20.0f, 6.0f, 0.0f });

  // An offset edited while playing is dropped on stop, and the cache must follow it back.
  hull->start();
  hull->setPosition({ 0.0f, 9.0f, 0.0f });
  expectNear(hull->findFurthestPoint({ 0.0f, 1.0f, 0.0f }), { 20.0f, 10.0f, 0.0f });
  hull->stop();
  expectNear(hull->findFurthestPoint({ 0.0f, 1.0f, 0.0f }), { 20.0f, 6.0f, 0.0f });
}

TEST(ConvexMeshCollider, TheWorldVerticesGenerationBumpsOnlyWhenTheyRegenerate)
{
  const auto scene = makeScene();
  const auto object = fixtures::addObject(scene, "Hull");
  const auto hull = std::make_shared<ConvexMeshCollider>();
  object->addComponent(hull);

  static_cast<void>(hull->getWorldVertices());
  const auto first = hull->getWorldVerticesGeneration();
  static_cast<void>(hull->getWorldVertices());
  EXPECT_EQ(hull->getWorldVerticesGeneration(), first);

  fixtures::transformOf(object)->setPosition({ 1.0f, 2.0f, 3.0f });
  static_cast<void>(hull->getWorldVertices());
  EXPECT_GT(hull->getWorldVerticesGeneration(), first);

  const auto second = hull->getWorldVerticesGeneration();
  hull->setScale({ 2.0f, 2.0f, 2.0f });
  static_cast<void>(hull->getWorldVertices());
  EXPECT_GT(hull->getWorldVerticesGeneration(), second);
}

TEST(ConvexMeshCollider, TheMassCacheIsDerivedStateOutsideTheSavedForm)
{
  ConvexMeshCollider::MassCache cache;
  cache.volume = 4.0f;

  const auto source = customCollider();
  EXPECT_FALSE(source->getMassCache().has_value());
  source->setMassCache(cache);
  ASSERT_TRUE(source->getMassCache().has_value());
  EXPECT_FLOAT_EQ(source->getMassCache()->volume, 4.0f);

  ConvexMeshCollider loaded;
  loaded.loadFromJSON(source->serialize());
  EXPECT_FALSE(loaded.getMassCache().has_value());

  ConvexMeshCollider fromWire;
  unpackInto(fromWire, packed(*source));
  EXPECT_FALSE(fromWire.getMassCache().has_value());

  source->setPosition({ 5.0f, 0.0f, 0.0f });
  EXPECT_FALSE(source->getMassCache().has_value());
}
