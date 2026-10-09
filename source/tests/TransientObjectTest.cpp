#include <gtest/gtest.h>

#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"

#include <TransientObject.h>

#include <glm/vec3.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <uuid.h>

namespace {
  std::string makeBody(const std::string& name)
  {
    fixtures::Scene authoring;
    const auto root = fixtures::addObject(authoring, name, glm::vec3(1, 2, 3));
    fixtures::addChildObject(authoring, name + " Child", root);

    return root->serialize().dump();
  }
}

TEST(TransientObject, StartsWithNoObjectOrManager)
{
  const fixtures::Scene scene;
  const TransientObject transient(scene.componentRegistry);

  EXPECT_EQ(transient.object(), nullptr);
  EXPECT_EQ(transient.manager(), nullptr);
}

TEST(TransientObject, SerializeWithNoObjectIsEmpty)
{
  const fixtures::Scene scene;
  const TransientObject transient(scene.componentRegistry);

  EXPECT_TRUE(transient.serialize().empty());
}

TEST(TransientObject, SyncBuildsTheObjectAndPreservesUuids)
{
  const fixtures::Scene scene;
  TransientObject transient(scene.componentRegistry);
  const auto body = makeBody("Block");
  const auto parsed = nlohmann::json::parse(body);

  EXPECT_TRUE(transient.syncFromBody(body));

  const auto object = transient.object();
  ASSERT_NE(object, nullptr);
  EXPECT_EQ(object->getName(), "Block");
  EXPECT_EQ(uuids::to_string(object->getUUID()), parsed.at("uuid").get<std::string>());
  ASSERT_EQ(object->getChildren().size(), 1u);
  EXPECT_EQ(uuids::to_string(object->getChildren().front()->getUUID()),
            parsed.at("children").at(0).at("uuid").get<std::string>());
}

TEST(TransientObject, ManagerOwnsTheObjectAndItsChildren)
{
  const fixtures::Scene scene;
  TransientObject transient(scene.componentRegistry);

  ASSERT_TRUE(transient.syncFromBody(makeBody("Block")));

  ASSERT_NE(transient.manager(), nullptr);
  EXPECT_EQ(transient.object()->getManager(), transient.manager());
  const auto child = transient.object()->getChildren().front();
  EXPECT_EQ(transient.manager()->getObjectByUUID(transient.object()->getUUID()), transient.object());
  EXPECT_EQ(transient.manager()->getObjectByUUID(child->getUUID()), child);
}

TEST(TransientObject, RigidBodyComponentRoundTripsThroughSyncAndSerialize)
{
  const fixtures::Scene scene;
  TransientObject transient(scene.componentRegistry);
  fixtures::Scene authoring;
  const auto root = fixtures::addObject(authoring, "Body", glm::vec3(0));
  fixtures::addRigidBody(root);
  const auto body = root->serialize();

  ASSERT_TRUE(transient.syncFromBody(body.dump()));

  const auto findRigidBody = [](const nlohmann::json& object)
  {
    for (const auto& component : object.at("components"))
    {
      if (component.at("type").get<std::string>() == "RigidBody")
      {
        return component;
      }
    }
    return nlohmann::json();
  };

  const auto expected = findRigidBody(body);
  ASSERT_FALSE(expected.is_null());
  EXPECT_EQ(findRigidBody(nlohmann::json::parse(transient.serialize())), expected);
}

TEST(TransientObject, UnknownComponentTypeLeavesNoObjectAndRecovers)
{
  const fixtures::Scene scene;
  TransientObject transient(scene.componentRegistry);
  auto body = nlohmann::json::parse(makeBody("Block"));
  body["components"].push_back({ { "type", "NoSuchComponent" } });

  EXPECT_TRUE(transient.syncFromBody(body.dump()));

  EXPECT_EQ(transient.object(), nullptr);
  EXPECT_EQ(transient.manager(), nullptr);

  EXPECT_TRUE(transient.syncFromBody(makeBody("Block")));
  EXPECT_NE(transient.object(), nullptr);
}

TEST(TransientObject, MalformedUuidLeavesNoObjectAndRecovers)
{
  const fixtures::Scene scene;
  TransientObject transient(scene.componentRegistry);
  auto body = nlohmann::json::parse(makeBody("Block"));
  body["uuid"] = "not-a-uuid";

  EXPECT_TRUE(transient.syncFromBody(body.dump()));

  EXPECT_EQ(transient.object(), nullptr);
  EXPECT_EQ(transient.manager(), nullptr);

  EXPECT_TRUE(transient.syncFromBody(makeBody("Block")));
  EXPECT_NE(transient.object(), nullptr);
}

TEST(TransientObject, SameBodyAgainDoesNotRebuild)
{
  const fixtures::Scene scene;
  TransientObject transient(scene.componentRegistry);
  const auto body = makeBody("Block");

  ASSERT_TRUE(transient.syncFromBody(body));
  const auto first = transient.object();

  EXPECT_FALSE(transient.syncFromBody(body));
  EXPECT_EQ(transient.object(), first);
}

TEST(TransientObject, MarkSyncedSuppressesTheEchoAndKeepsTheEditedObject)
{
  const fixtures::Scene scene;
  TransientObject transient(scene.componentRegistry);
  ASSERT_TRUE(transient.syncFromBody(makeBody("Block")));
  const auto edited = transient.object();

  edited->setName("Renamed");
  const auto editedBody = transient.serialize();
  EXPECT_EQ(nlohmann::json::parse(editedBody).at("name").get<std::string>(), "Renamed");
  transient.markSynced(editedBody);

  EXPECT_FALSE(transient.syncFromBody(editedBody));
  EXPECT_EQ(transient.object(), edited);
  EXPECT_EQ(transient.object()->getName(), "Renamed");
}

TEST(TransientObject, DifferentBodyRebuildsAfterMarkSynced)
{
  const fixtures::Scene scene;
  TransientObject transient(scene.componentRegistry);
  ASSERT_TRUE(transient.syncFromBody(makeBody("Block")));
  transient.object()->setName("Renamed");
  transient.markSynced(transient.serialize());

  EXPECT_TRUE(transient.syncFromBody(makeBody("Sphere")));

  ASSERT_NE(transient.object(), nullptr);
  EXPECT_EQ(transient.object()->getName(), "Sphere");
}

TEST(TransientObject, MalformedJsonClearsTheObjectAndIsRemembered)
{
  const fixtures::Scene scene;
  TransientObject transient(scene.componentRegistry);
  ASSERT_TRUE(transient.syncFromBody(makeBody("Block")));
  ASSERT_NE(transient.object(), nullptr);

  EXPECT_TRUE(transient.syncFromBody("{ not json"));

  EXPECT_EQ(transient.object(), nullptr);
  EXPECT_EQ(transient.manager(), nullptr);
  EXPECT_TRUE(transient.serialize().empty());
  EXPECT_FALSE(transient.syncFromBody("{ not json"));
}

TEST(TransientObject, NonObjectJsonAndIncompleteBodiesLeaveNoObject)
{
  const fixtures::Scene scene;
  TransientObject transient(scene.componentRegistry);

  EXPECT_TRUE(transient.syncFromBody("[1, 2, 3]"));
  EXPECT_EQ(transient.object(), nullptr);

  EXPECT_TRUE(transient.syncFromBody("{}"));
  EXPECT_EQ(transient.object(), nullptr);
  EXPECT_EQ(transient.manager(), nullptr);

  EXPECT_TRUE(transient.syncFromBody(""));
  EXPECT_EQ(transient.object(), nullptr);
}

TEST(TransientObject, ValidBodyAfterMalformedBuildsAgain)
{
  const fixtures::Scene scene;
  TransientObject transient(scene.componentRegistry);
  ASSERT_TRUE(transient.syncFromBody("{ not json"));
  ASSERT_EQ(transient.object(), nullptr);

  EXPECT_TRUE(transient.syncFromBody(makeBody("Block")));

  ASSERT_NE(transient.object(), nullptr);
  EXPECT_EQ(transient.object()->getName(), "Block");
  EXPECT_NE(transient.manager(), nullptr);
}
