#include <gtest/gtest.h>

#include "ConvexMeshFixtures.h"
#include "TestScene.h"
#include "Replication.h"
#include "WireTypes.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "objects/components/Script.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/ConvexHullValidation.h"
#include "objects/components/collisions/ConvexMeshCollider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <uuid.h>

namespace {
  using fixtures::transformOf;

  // Every test here edits an object that is already in the scene, so the fixture carries one.
  struct Scene : fixtures::Scene {
    std::shared_ptr<Object> object;
  };

  Scene makeScene()
  {
    Scene scene;
    scene.object = addObject(scene, "Object");

    return scene;
  }

  net::Message withoutTheLastBytes(const net::Message& source, const std::size_t dropped)
  {
    net::Message result(net::MessageType::editComponent);

    const auto bytes = source.bytes();
    for (std::size_t i = 0; i + dropped < bytes.size(); ++i)
    {
      result.write(bytes[i]);
    }

    return result;
  }

  // A convex mesh edit with the given offset and hull sizes, written in pack order. A count over the
  // collider's limit stops after the count, since the collider refuses it before reading more.
  net::Message convexEdit(const uuids::uuid& target, const glm::vec3& position, const std::uint32_t vertexCount,
                          const std::uint32_t indexCount)
  {
    net::Message edit(net::MessageType::editComponent);
    edit.writeString(uuids::to_string(target));
    edit.write(ComponentType::SubComponentType_convexMeshCollider);
    edit.write(false);
    edit.write(position);
    edit.write(glm::vec3(1.0f));
    edit.write(glm::vec3(0.0f));
    edit.write(uuids::uuid{});

    const auto cube = makeUnitCubeHull();
    edit.write(vertexCount);
    if (vertexCount > maxConvexHullVertices)
    {
      return edit;
    }

    for (std::uint32_t i = 0; i < vertexCount && i < cube.vertices.size(); ++i)
    {
      edit.write(cube.vertices[i]);
    }

    edit.write(indexCount);
    if (indexCount > maxConvexHullIndices)
    {
      return edit;
    }

    for (std::uint32_t i = 0; i < indexCount && i < cube.indices.size(); ++i)
    {
      edit.write(cube.indices[i]);
    }

    edit.write(false);
    edit.write(std::uint32_t{ 0 });
    edit.write(std::uint32_t{ 0xFFFFFFFFu });

    return edit;
  }
}

TEST(ComponentEdit, AppliesAnEditForAKnownObject)
{
  const auto scene = makeScene();
  const auto transform = transformOf(scene.object);

  transform->setPosition({ 1, 2, 3 });
  const auto edit = replication::buildComponentEdit(scene.object->getUUID(), transform);
  transform->setPosition({ 0, 0, 0 });

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::applied);
  EXPECT_EQ(transform->getLocalPosition(), glm::vec3(1, 2, 3));
}

TEST(ComponentEdit, ReportsAUuidThatDoesNotParseAsMalformed)
{
  const auto scene = makeScene();

  net::Message edit(net::MessageType::editComponent);
  edit.writeString("not-a-uuid");

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::malformedPayload);
}

TEST(ComponentEdit, ReportsATruncatedPayloadAsMalformed)
{
  const auto scene = makeScene();

  // A well-formed uuid and nothing after it, so the reader runs off the end of the payload.
  net::Message edit(net::MessageType::editComponent);
  edit.writeString(uuids::to_string(scene.object->getUUID()));

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::malformedPayload);
}

TEST(ComponentEdit, ReportsAPayloadThatRunsOutMidComponentAsPartiallyApplied)
{
  const auto scene = makeScene();

  const auto collider = std::make_shared<BoxCollider>();
  scene.object->addComponent(collider);

  collider->setScale({ 4, 5, 6 });
  const auto edit = replication::buildComponentEdit(scene.object->getUUID(), collider);
  collider->setScale({ 1, 1, 1 });

  // A component unpacks field by field as it reads, so losing the tail leaves it half written - which is
  // a different problem from a payload that never started applying.
  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, withoutTheLastBytes(edit, 4)),
            replication::ComponentEditResult::partiallyApplied);
  EXPECT_EQ(collider->getLocalScale(), glm::vec3(4, 5, 6));
}

TEST(ComponentEdit, ReportsAnObjectItDoesNotHaveAsUnknown)
{
  const auto scene = makeScene();

  // Built against another scene, which is what a view legitimately sees when the server rebroadcasts an
  // edit for an object it has not been sent yet or has already dropped.
  const auto elsewhere = makeScene();
  const auto edit = replication::buildComponentEdit(elsewhere.object->getUUID(), transformOf(elsewhere.object));

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::unknownObject);
}

TEST(ComponentEdit, ReportsAComponentTheObjectDoesNotHaveAsUnknown)
{
  const auto scene = makeScene();

  // The uuid resolves, but the object it names carries only a Transform.
  const auto edit = replication::buildComponentEdit(scene.object->getUUID(),
                                                    std::make_shared<BoxCollider>());

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::unknownComponent);
}

TEST(ComponentEdit, MapsAColliderSubtypeBackToTheComponentItIsStoredUnder)
{
  const auto scene = makeScene();

  const auto collider = std::make_shared<BoxCollider>();
  scene.object->addComponent(collider);

  collider->setScale({ 4, 5, 6 });
  const auto edit = replication::buildComponentEdit(scene.object->getUUID(), collider);
  collider->setScale({ 1, 1, 1 });

  // A collider packs its subtype as the discriminator but is stored under the parent collider type, so
  // this is the one component whose lookup needs a mapping step.
  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::applied);
  EXPECT_EQ(collider->getLocalScale(), glm::vec3(4, 5, 6));
}

TEST(ComponentEdit, FindsTheRightScriptByClassName)
{
  const auto scene = makeScene();

  const auto first = std::make_shared<Script>();
  first->setClassName("First");
  scene.object->addComponent(first);

  const auto second = std::make_shared<Script>();
  second->setClassName("Second");
  scene.object->addComponent(second);

  second->setFields(nlohmann::json{ { "speed", 4 } });
  const auto edit = replication::buildComponentEdit(scene.object->getUUID(), second);
  second->setFields(nlohmann::json::object());

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::applied);
  EXPECT_EQ(second->getFields().at("speed"), 4);
  EXPECT_TRUE(first->getFields().empty());
}

TEST(ComponentEdit, ReportsAScriptClassTheObjectDoesNotCarryAsUnknown)
{
  const auto scene = makeScene();

  const auto present = std::make_shared<Script>();
  present->setClassName("Present");
  scene.object->addComponent(present);

  const auto absent = std::make_shared<Script>();
  absent->setClassName("Absent");

  const auto edit = replication::buildComponentEdit(scene.object->getUUID(), absent);

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::unknownComponent);
}

TEST(ComponentEdit, RefusesADiscriminatorThatNamesNoComponent)
{
  const auto scene = makeScene();

  net::Message edit(net::MessageType::editComponent);
  edit.writeString(uuids::to_string(scene.object->getUUID()));
  edit.write(static_cast<ComponentType>(0x7F));

  // A discriminator off the wire has to name something that is actually packed before it picks a slot.
  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::malformedPayload);
}

TEST(ComponentEdit, RefusesTheParentColliderTypeAsADiscriminator)
{
  const auto scene = makeScene();

  const auto box = std::make_shared<BoxCollider>();
  scene.object->addComponent(box);
  box->setScale({ 4, 5, 6 });

  net::Message edit(net::MessageType::editComponent);
  edit.writeString(uuids::to_string(scene.object->getUUID()));
  edit.write(ComponentType::collider);

  // The case that made the check necessary: colliders pack their shape, never the parent type, but the
  // parent type is a valid key in the component map - so this used to unpack into whichever shape was
  // there, reading a body of the wrong layout.
  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::malformedPayload);
  EXPECT_EQ(box->getLocalScale(), glm::vec3(4, 5, 6));
}

TEST(ComponentEdit, RefusesAnEditForTheOtherColliderShape)
{
  const auto scene = makeScene();

  const auto sphere = std::make_shared<SphereCollider>();
  scene.object->addComponent(sphere);
  sphere->setRadius(5.0f);

  // Both shapes live under the same map key, so this resolves to the sphere - but a box body read as a
  // sphere takes a 12-byte vector where a 4-byte float belongs, corrupting it silently.
  const auto edit = replication::buildComponentEdit(scene.object->getUUID(),
                                                    std::make_shared<BoxCollider>());

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::unknownComponent);
  EXPECT_FLOAT_EQ(sphere->getLocalRadius(), 5.0f);
}

TEST(ComponentEdit, ARefusedConvexMeshPayloadIsMalformedAndChangesNothing)
{
  const auto scene = makeScene();
  const auto hull = std::make_shared<ConvexMeshCollider>();
  scene.object->addComponent(hull);
  const auto uuid = scene.object->getUUID();
  const auto cubeVertices = static_cast<std::uint32_t>(makeUnitCubeHull().vertices.size());
  const auto cubeIndices = static_cast<std::uint32_t>(makeUnitCubeHull().indices.size());
  const auto malformed = replication::ComponentEditResult::malformedPayload;

  // The control: the same builder with a finite offset and the whole cube lands.
  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager,
                                            convexEdit(uuid, { 1, 2, 3 }, cubeVertices, cubeIndices)),
            replication::ComponentEditResult::applied);
  EXPECT_EQ(hull->getLocalPosition(), glm::vec3(1, 2, 3));

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager,
                                            convexEdit(uuid, { 9, 9, 9 }, 0xFFFFFFFFu, 0)), malformed);
  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager,
                                            convexEdit(uuid, { 9, 9, 9 }, cubeVertices, 0xFFFFFFFFu)), malformed);
  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager,
                                            convexEdit(uuid, { 9, 9, 9 }, cubeVertices, 0)), malformed);
  EXPECT_EQ(replication::applyComponentEdit(
              *scene.objectManager,
              convexEdit(uuid, { std::numeric_limits<float>::infinity(), 0, 0 }, cubeVertices, cubeIndices)),
            malformed);

  EXPECT_EQ(hull->getLocalPosition(), glm::vec3(1, 2, 3));
}

TEST(ComponentEdit, ATruncatedConvexMeshPayloadIsStillPartiallyApplied)
{
  const auto scene = makeScene();
  scene.object->addComponent(std::make_shared<ConvexMeshCollider>());
  const auto cubeVertices = static_cast<std::uint32_t>(makeUnitCubeHull().vertices.size());
  const auto cubeIndices = static_cast<std::uint32_t>(makeUnitCubeHull().indices.size());

  const auto whole = convexEdit(scene.object->getUUID(), { 1, 2, 3 }, cubeVertices, cubeIndices);
  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, whole),
            replication::ComponentEditResult::applied);

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, withoutTheLastBytes(whole, 1)),
            replication::ComponentEditResult::partiallyApplied);
}
