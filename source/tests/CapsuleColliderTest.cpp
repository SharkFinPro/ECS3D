#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "TestPrinters.h"
#include "TestScene.h"
#include "WireTypes.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/CapsuleCollider.h"

#include <Protocol.h>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
  using fixtures::expectNear;

  constexpr float tolerance = 1e-4f;

  // The Object is the collider's lifetime anchor: it holds the Transform the collider resolves through.
  std::pair<std::shared_ptr<Object>, std::shared_ptr<CapsuleCollider>> makeCapsule(
    const glm::vec3& position = glm::vec3(0), const glm::vec3& scale = glm::vec3(1))
  {
    auto object = std::make_shared<Object>("Collider");
    const auto transform = object->getComponent<Transform>(ComponentType::transform);
    transform->setPosition(position);
    transform->setScale(scale);

    auto capsule = std::make_shared<CapsuleCollider>();
    object->addComponent(capsule);

    return { object, capsule };
  }

  std::shared_ptr<Transform> transformOf(const std::shared_ptr<Object>& object)
  {
    return object->getComponent<Transform>(ComponentType::transform);
  }

  net::Message packed(const CapsuleCollider& capsule)
  {
    net::Message message(net::MessageType::undefined);
    capsule.pack(message);

    return message;
  }

  // pack writes the type discriminator first; unpack expects the reader positioned after it.
  void unpackInto(CapsuleCollider& capsule, const net::Message& message)
  {
    net::MessageReader reader(message);
    static_cast<void>(reader.read<ComponentType>());
    capsule.unpack(reader);
  }

  void expectSameShape(CapsuleCollider& actual, CapsuleCollider& expected)
  {
    expectNear("position", actual.getLocalPosition(), expected.getLocalPosition());
    expectNear("rotation", actual.getLocalRotation(), expected.getLocalRotation());
    EXPECT_EQ(actual.getLocalRadius(), expected.getLocalRadius());
    EXPECT_EQ(actual.getLocalHeight(), expected.getLocalHeight());
    EXPECT_EQ(actual.getRenderCollider(), expected.getRenderCollider());
    EXPECT_EQ(actual.isTrigger(), expected.isTrigger());
    EXPECT_EQ(actual.getLayer(), expected.getLayer());
    EXPECT_EQ(actual.getMask(), expected.getMask());
  }

  std::shared_ptr<CapsuleCollider> customized()
  {
    auto capsule = std::make_shared<CapsuleCollider>();
    capsule->setPosition({ 1.0f, -2.0f, 3.0f });
    capsule->setRotation({ 10.0f, 20.0f, 30.0f });
    capsule->setRadius(0.75f);
    capsule->setHeight(3.5f);
    capsule->setRenderCollider(true);
    capsule->setIsTrigger(true);
    capsule->setLayer(5);
    capsule->setMask(0xF0F0u);

    return capsule;
  }

  float width(const BoundingBox& bounds)
  {
    return bounds.maxX - bounds.minX;
  }

  float height(const BoundingBox& bounds)
  {
    return bounds.maxY - bounds.minY;
  }
}

TEST(CapsuleCollider, StartsUprightWithTheDocumentedDefaults)
{
  const auto [object, capsule] = makeCapsule();

  EXPECT_EQ(capsule->getColliderType(), ColliderType::capsuleCollider);
  EXPECT_EQ(capsule->getSubType(), ComponentType::SubComponentType_capsuleCollider);
  EXPECT_EQ(capsule->getLocalRadius(), 0.5f);
  EXPECT_EQ(capsule->getLocalHeight(), 2.0f);
  EXPECT_EQ(capsule->getLocalPosition(), glm::vec3(0));
  EXPECT_EQ(capsule->getLocalRotation(), glm::vec3(0));
}

TEST(CapsuleCollider, TheRegistryCreatesOneByItsKey)
{
  ComponentRegistry registry;
  registerDataComponents(registry);

  const auto component = registry.create("Capsule");
  ASSERT_NE(component, nullptr);
  EXPECT_NE(std::dynamic_pointer_cast<CapsuleCollider>(component), nullptr);
  EXPECT_EQ(componentTypeToRegistryKey.at(ComponentType::SubComponentType_capsuleCollider), "Capsule");
  EXPECT_EQ(componentTypeToString.at(ComponentType::SubComponentType_capsuleCollider), "Capsule Collider");
  EXPECT_EQ(subComponentTypeToParent.at(ComponentType::SubComponentType_capsuleCollider), ComponentType::collider);
}

TEST(CapsuleCollider, ItsWireDiscriminatorComesAfterEveryExistingType)
{
  // The packed value is the wire discriminator, so it may not move an existing type.
  EXPECT_GT(static_cast<uint8_t>(ComponentType::SubComponentType_capsuleCollider),
            static_cast<uint8_t>(ComponentType::camera));
  EXPECT_LT(static_cast<uint8_t>(ComponentType::SubComponentType_capsuleCollider),
            static_cast<uint8_t>(ComponentType::count));
}

TEST(CapsuleCollider, ItsJsonSaysCapsuleAndRoundTrips)
{
  const auto source = customized();
  const auto saved = source->serialize();

  EXPECT_EQ(saved.at("type"), "Collider");
  EXPECT_EQ(saved.at("subType"), "Capsule");

  CapsuleCollider loaded;
  loaded.loadFromJSON(saved);

  expectSameShape(loaded, *source);
}

TEST(CapsuleCollider, ItRoundTripsThroughTheWire)
{
  const auto source = customized();

  CapsuleCollider loaded;
  unpackInto(loaded, packed(*source));

  expectSameShape(loaded, *source);

  // Positive control: the default capsule really differs from the one that was sent.
  CapsuleCollider untouched;
  EXPECT_NE(untouched.getLocalRadius(), source->getLocalRadius());
  EXPECT_NE(untouched.getLocalHeight(), source->getLocalHeight());
}

TEST(CapsuleCollider, AJsonBlobWithoutTheFlagsLoadsWithTheirDefaults)
{
  auto saved = customized()->serialize();
  saved.erase("renderCollider");
  saved.erase("isTrigger");
  saved.erase("layer");
  saved.erase("mask");

  CapsuleCollider loaded;
  loaded.loadFromJSON(saved);

  EXPECT_FALSE(loaded.getRenderCollider());
  EXPECT_FALSE(loaded.isTrigger());
  EXPECT_EQ(loaded.getLayer(), 0u);
  EXPECT_EQ(loaded.getMask(), 0xFFFFFFFFu);
  EXPECT_EQ(loaded.getLocalRadius(), 0.75f);
}

TEST(CapsuleCollider, AJsonBlobWithoutAShapeFieldIsRefused)
{
  const auto saved = customized()->serialize();

  for (const char* key : { "position", "rotation", "radius", "height" })
  {
    auto damaged = saved;
    damaged.erase(key);

    CapsuleCollider loaded;
    EXPECT_THROW(loaded.loadFromJSON(damaged), std::exception) << key;
  }

  CapsuleCollider control;
  EXPECT_NO_THROW(control.loadFromJSON(saved));
}

TEST(CapsuleCollider, EveryTruncatedWirePayloadIsRefused)
{
  const auto full = packed(*customized());
  const auto bytes = full.bytes();
  const std::size_t discriminator = sizeof(ComponentType);

  ASSERT_GT(bytes.size(), discriminator);

  for (std::size_t length = discriminator; length < bytes.size(); ++length)
  {
    net::Message prefix(net::MessageType::undefined);
    for (std::size_t i = 0; i < length; ++i)
    {
      prefix.write(static_cast<uint8_t>(bytes[i]));
    }

    CapsuleCollider loaded;
    EXPECT_THROW(unpackInto(loaded, prefix), std::runtime_error) << "prefix of " << length << " bytes";
  }

  // Positive control: the whole payload unpacks.
  CapsuleCollider loaded;
  EXPECT_NO_THROW(unpackInto(loaded, full));
}

TEST(CapsuleCollider, AnUprightCapsuleSpansItsHeightAndDiameter)
{
  const auto [object, capsule] = makeCapsule({ 1, 2, 3 });

  // Half-height 1, radius 0.5: the core segment runs 0.5 either side of the center.
  const auto [start, end] = capsule->getSegment();
  expectNear("start", start, { 1, 1.5f, 3 }, tolerance);
  expectNear("end", end, { 1, 2.5f, 3 }, tolerance);
  EXPECT_FLOAT_EQ(capsule->getRadius(), 0.5f);

  expectNear("top", capsule->findFurthestPoint({ 0, 1, 0 }), { 1, 3, 3 }, tolerance);
  expectNear("bottom", capsule->findFurthestPoint({ 0, -1, 0 }), { 1, 1, 3 }, tolerance);

  const auto& bounds = capsule->getBoundingBox();
  EXPECT_NEAR(width(bounds), 1.0f, tolerance);
  EXPECT_NEAR(height(bounds), 2.0f, tolerance);
  EXPECT_NEAR(bounds.minY, 1.0f, tolerance);
}

TEST(CapsuleCollider, ASideDirectionPicksTheNearerEndAndPushesOutByTheRadius)
{
  const auto [object, capsule] = makeCapsule();

  // Tilted off the axis so the two ends do not tie on the dot product.
  const glm::vec3 direction = glm::normalize(glm::vec3(1.0f, 0.1f, 0.0f));
  const glm::vec3 expected = glm::vec3(0, 0.5f, 0) + direction * 0.5f;

  expectNear("support", capsule->findFurthestPoint(direction), expected, tolerance);

  const glm::vec3 below = glm::normalize(glm::vec3(0.0f, -1.0f, 2.0f));
  expectNear("support below", capsule->findFurthestPoint(below), glm::vec3(0, -0.5f, 0) + below * 0.5f, tolerance);
}

TEST(CapsuleCollider, ASupportDirectionNeedNotBeUnitLength)
{
  const auto [object, capsule] = makeCapsule();

  expectNear("scaled direction", capsule->findFurthestPoint({ 0, 10, 0 }), { 0, 1, 0 }, tolerance);
  expectNear("zero direction", capsule->findFurthestPoint({ 0, 0, 0 }), { 0, 0, 0 }, tolerance);
}

TEST(CapsuleCollider, ARotatedCapsuleLiesAlongTheRotatedAxis)
{
  const auto [object, capsule] = makeCapsule();
  transformOf(object)->setRotation({ 0, 0, 90 });

  // Turning +y by 90 degrees about z gives -x.
  const auto [start, end] = capsule->getSegment();
  expectNear("start", start, { 0.5f, 0, 0 }, tolerance);
  expectNear("end", end, { -0.5f, 0, 0 }, tolerance);

  const auto& bounds = capsule->getBoundingBox();
  EXPECT_NEAR(bounds.minX, -1.0f, tolerance);
  EXPECT_NEAR(bounds.maxX, 1.0f, tolerance);
  EXPECT_NEAR(bounds.minY, -0.5f, tolerance);
  EXPECT_NEAR(bounds.maxY, 0.5f, tolerance);
}

TEST(CapsuleCollider, TheTransformAndColliderRotationsAddWithoutAParent)
{
  const auto [object, capsule] = makeCapsule();
  transformOf(object)->setRotation({ 0, 0, 45 });
  capsule->setRotation({ 0, 0, 45 });

  EXPECT_EQ(capsule->getRotation(), glm::vec3(0, 0, 90));

  const auto [start, end] = capsule->getSegment();
  expectNear("end", end, { -0.5f, 0, 0 }, tolerance);
  expectNear("start", start, { 0.5f, 0, 0 }, tolerance);
}

TEST(CapsuleCollider, TheOffsetStaysOnTheWorldAxes)
{
  const auto [object, capsule] = makeCapsule({ 10, 0, 0 });
  transformOf(object)->setRotation({ 0, 0, 90 });
  capsule->setPosition({ 1, 2, 3 });

  EXPECT_EQ(capsule->getPosition(), glm::vec3(11, 2, 3));

  const auto [start, end] = capsule->getSegment();
  expectNear("midpoint", (start + end) * 0.5f, { 11, 2, 3 }, tolerance);
}

TEST(CapsuleCollider, ObjectScaleSizesTheRadiusFromXzAndTheLengthFromY)
{
  const auto [object, capsule] = makeCapsule({ 0, 0, 0 }, { 2, 3, 1 });

  // Radius 0.5 * max(2, 1) = 1; half-height 1 * 3 = 3, so the segment runs 2 either side.
  EXPECT_NEAR(capsule->getRadius(), 1.0f, tolerance);
  const auto [start, end] = capsule->getSegment();
  expectNear("start", start, { 0, -2, 0 }, tolerance);
  expectNear("end", end, { 0, 2, 0 }, tolerance);

  const auto& bounds = capsule->getBoundingBox();
  EXPECT_NEAR(bounds.maxY, 3.0f, tolerance);
  EXPECT_NEAR(bounds.maxX, 1.0f, tolerance);
}

TEST(CapsuleCollider, ATallRadiusLeavesASphere)
{
  const auto [object, capsule] = makeCapsule({ 0, 0, 0 }, { 3, 1, 2 });

  // Radius 1.5 is already past the half-height of 1, so there is no core segment left.
  const auto [start, end] = capsule->getSegment();
  EXPECT_EQ(start, end);
  EXPECT_NEAR(capsule->getRadius(), 1.5f, tolerance);

  const auto& bounds = capsule->getBoundingBox();
  EXPECT_NEAR(bounds.maxY, 1.5f, tolerance);
  EXPECT_NEAR(bounds.minY, -1.5f, tolerance);
}

TEST(CapsuleCollider, AHeightBelowTheDiameterDegeneratesToASphere)
{
  const auto [object, capsule] = makeCapsule();
  capsule->setHeight(0.5f);

  const auto [start, end] = capsule->getSegment();
  EXPECT_EQ(start, end);
  expectNear("top", capsule->findFurthestPoint({ 0, 1, 0 }), { 0, 0.5f, 0 }, tolerance);
}

TEST(CapsuleCollider, AChildComposesItsRotationThroughTheParent)
{
  auto parent = std::make_shared<Object>("Parent");
  transformOf(parent)->setPosition({ 10, 0, 0 });
  transformOf(parent)->setRotation({ 0, 0, 90 });

  auto child = std::make_shared<Object>("Child");
  child->setParent(parent);
  parent->addChild(child);
  const auto capsule = std::make_shared<CapsuleCollider>();
  child->addComponent(capsule);

  const auto [start, end] = capsule->getSegment();
  expectNear("start", start, { 10.5f, 0, 0 }, tolerance);
  expectNear("end", end, { 9.5f, 0, 0 }, tolerance);

  const auto& bounds = capsule->getBoundingBox();
  EXPECT_NEAR(bounds.minX, 9.0f, tolerance);
  EXPECT_NEAR(bounds.maxX, 11.0f, tolerance);
}

TEST(CapsuleCollider, MovingTheParentMovesAChildCapsuleBounds)
{
  fixtures::Scene scene;
  const auto parent = fixtures::addObject(scene, "Parent");
  const auto child = fixtures::addChildObject(scene, "Child", parent);
  const auto capsule = fixtures::addCapsuleCollider(child, 0.5f, 2.0f);

  const float before = capsule->getBoundingBox().maxX;

  fixtures::transformOf(parent)->setPosition({ 4, 0, 0 });
  EXPECT_NEAR(capsule->getBoundingBox().maxX, before + 4.0f, tolerance);

  fixtures::transformOf(parent)->setScale({ 2, 2, 2 });
  EXPECT_NEAR(width(capsule->getBoundingBox()), 2.0f, tolerance);
}

TEST(CapsuleCollider, EverySetterInvalidatesAWarmBoundingBox)
{
  const auto [object, capsule] = makeCapsule();
  EXPECT_NEAR(capsule->getBoundingBox().maxX, 0.5f, tolerance);
  EXPECT_NEAR(capsule->getBoundingBox().maxY, 1.0f, tolerance);

  capsule->setRadius(1.0f);
  EXPECT_NEAR(capsule->getBoundingBox().maxX, 1.0f, tolerance);

  // Radius 1 with height 2 is a sphere; the taller height gives a segment again.
  capsule->setHeight(4.0f);
  EXPECT_NEAR(capsule->getBoundingBox().maxY, 2.0f, tolerance);

  capsule->setPosition({ 3, 0, 0 });
  EXPECT_NEAR(capsule->getBoundingBox().maxX, 4.0f, tolerance);

  capsule->setPosition({ 0, 0, 0 });
  capsule->setRotation({ 0, 0, 90 });
  EXPECT_NEAR(capsule->getBoundingBox().maxX, 2.0f, tolerance);
  EXPECT_NEAR(capsule->getBoundingBox().maxY, 1.0f, tolerance);
}

TEST(CapsuleCollider, LoadingAndUnpackingInvalidateAWarmBoundingBox)
{
  const auto [object, capsule] = makeCapsule();
  EXPECT_NEAR(capsule->getBoundingBox().maxX, 0.5f, tolerance);

  const auto wide = customized();
  wide->setPosition({ 0, 0, 0 });
  wide->setRotation({ 0, 0, 0 });
  wide->setRadius(2.0f);
  capsule->loadFromJSON(wide->serialize());
  EXPECT_NEAR(capsule->getBoundingBox().maxX, 2.0f, tolerance);

  const auto [otherObject, other] = makeCapsule();
  EXPECT_NEAR(other->getBoundingBox().maxX, 0.5f, tolerance);
  unpackInto(*other, packed(*wide));
  EXPECT_NEAR(other->getBoundingBox().maxX, 2.0f, tolerance);
}

TEST(CapsuleCollider, RadiusAndHeightClampAtZeroAndIgnoreNonFiniteInput)
{
  CapsuleCollider capsule;

  capsule.setRadius(-3.0f);
  capsule.setHeight(-1.0f);
  EXPECT_EQ(capsule.getLocalRadius(), 0.0f);
  EXPECT_EQ(capsule.getLocalHeight(), 0.0f);

  capsule.setRadius(1.25f);
  capsule.setHeight(3.0f);
  capsule.setRadius(std::numeric_limits<float>::quiet_NaN());
  capsule.setHeight(std::numeric_limits<float>::infinity());
  capsule.setPosition({ std::numeric_limits<float>::infinity(), 0, 0 });
  capsule.setRotation({ 0, std::numeric_limits<float>::quiet_NaN(), 0 });

  EXPECT_EQ(capsule.getLocalRadius(), 1.25f);
  EXPECT_EQ(capsule.getLocalHeight(), 3.0f);
  EXPECT_EQ(capsule.getLocalPosition(), glm::vec3(0));
  EXPECT_EQ(capsule.getLocalRotation(), glm::vec3(0));
}
