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

TEST(TransientObject, ManagerIsPrivateAndOwnsTheObject)
{
  const fixtures::Scene scene;
  TransientObject transient(scene.componentRegistry);

  ASSERT_TRUE(transient.syncFromBody(makeBody("Block")));

  ASSERT_NE(transient.manager(), nullptr);
  EXPECT_NE(transient.manager(), scene.objectManager.get());
  EXPECT_EQ(transient.object()->getManager(), transient.manager());
  EXPECT_TRUE(scene.objectManager->getObjects().empty());
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
