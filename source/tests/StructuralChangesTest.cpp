#include <gtest/gtest.h>

#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/Transform.h"

#include <Protocol.h>
#include <StructuralChanges.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <uuid.h>
#include <vector>

namespace {
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::Scene;
  using fixtures::transformOf;

  // One ordered log of everything replicateStructuralChanges does: each taker call, each broadcast (with the
  // uuid it carries) and the notify call. The buffers are what the takers hand back, once.
  struct Harness
  {
    std::vector<std::string> events;
    std::vector<std::pair<net::MessageType, uuids::uuid>> sent;
    std::vector<std::pair<uuids::uuid, std::shared_ptr<Component>>> componentEdits;
    std::vector<std::shared_ptr<Object>> spawned;
    std::vector<uuids::uuid> destroyed;
    std::vector<uuids::uuid> changed;
    std::vector<uuids::uuid> notifiedSpawned;
    std::vector<uuids::uuid> notifiedDestroyed;
    std::function<void()> onNotify;
    std::function<std::string()> notifyState;
    std::function<void(const net::Message&)> onBroadcast;

    template <typename Buffer>
    static Buffer takeFrom(Buffer& buffer)
    {
      Buffer taken;
      std::swap(taken, buffer);
      return taken;
    }

    [[nodiscard]] static std::string typeName(const net::MessageType type)
    {
      switch (type)
      {
        case net::MessageType::editComponent:
          return "componentEdit";
        case net::MessageType::objectSpawned:
          return "spawned";
        case net::MessageType::objectDestroyed:
          return "destroyed";
        case net::MessageType::objectComponentsChanged:
          return "componentsChanged";
        default:
          return "other";
      }
    }

    void run(ObjectManager* objectManager)
    {
      replicateStructuralChanges(
        objectManager,
        [this]
        {
          events.emplace_back("takeComponentEdits");
          return takeFrom(componentEdits);
        },
        [this]
        {
          events.emplace_back("takeSpawned");
          return takeFrom(spawned);
        },
        [this]
        {
          events.emplace_back("takeDestroyed");
          return takeFrom(destroyed);
        },
        [this]
        {
          events.emplace_back("takeChanged");
          return takeFrom(changed);
        },
        [this](const net::Message& message)
        {
          net::MessageReader reader(message);
          const auto id = uuids::uuid::from_string(reader.readString());
          events.push_back(typeName(message.getType()));
          if (!id.has_value())
          {
            ADD_FAILURE() << "broadcast " << events.back() << " does not lead with a uuid string";
            return;
          }

          sent.emplace_back(message.getType(), id.value());
          if (onBroadcast)
          {
            onBroadcast(message);
          }
        },
        [this](const std::vector<uuids::uuid>& spawnedUUIDs, const std::vector<uuids::uuid>& destroyedUUIDs)
        {
          events.push_back(notifyState ? "notify(" + notifyState() + ")" : "notify");
          notifiedSpawned = spawnedUUIDs;
          notifiedDestroyed = destroyedUUIDs;
          if (onNotify)
          {
            onNotify();
          }
        });
    }

    [[nodiscard]] std::vector<net::MessageType> sentTypes() const
    {
      std::vector<net::MessageType> types;
      for (const auto& entry : sent)
      {
        types.push_back(entry.first);
      }

      return types;
    }

    [[nodiscard]] bool sentFor(const net::MessageType type, const uuids::uuid& id) const
    {
      return std::ranges::find(sent, std::make_pair(type, id)) != sent.end();
    }
  };

  bool isMember(const Scene& scene, const std::shared_ptr<Object>& object)
  {
    const auto& all = scene.objectManager->getAllObjects();
    return std::ranges::find(all, object) != all.end();
  }

  bool contains(const std::vector<uuids::uuid>& list, const uuids::uuid& id)
  {
    return std::ranges::find(list, id) != list.end();
  }
}

TEST(StructuralChanges, RunsInTheContractedOrder)
{
  const auto scene = makeScene();
  const auto live = addObject(scene, "Live");
  const auto doomed = addObject(scene, "Doomed");

  Harness harness;
  harness.componentEdits.emplace_back(live->getUUID(), transformOf(live));

  const auto spawned = std::make_shared<Object>("Spawned");
  {
    const ObjectManager::ScriptPassGuard guard(*scene.objectManager);
    scene.objectManager->addObject(spawned);
  }
  harness.spawned.push_back(spawned);

  scene.objectManager->removeObject(doomed);
  harness.destroyed.push_back(doomed->getUUID());
  harness.changed.push_back(live->getUUID());

  harness.notifyState = [&]
  {
    return std::string("spawnedMember=") + (isMember(scene, spawned) ? "1" : "0")
      + ",doomedMember=" + (isMember(scene, doomed) ? "1" : "0");
  };

  harness.run(scene.objectManager.get());

  const std::vector<std::string> expected = {
    "takeComponentEdits", "componentEdit", "takeSpawned", "spawned", "takeDestroyed", "destroyed",
    "notify(spawnedMember=1,doomedMember=0)", "takeChanged", "componentsChanged"
  };
  EXPECT_EQ(harness.events, expected);
  EXPECT_TRUE(harness.sentFor(net::MessageType::editComponent, live->getUUID()));
  EXPECT_TRUE(harness.sentFor(net::MessageType::objectSpawned, spawned->getUUID()));
  EXPECT_TRUE(harness.sentFor(net::MessageType::objectDestroyed, doomed->getUUID()));
  EXPECT_TRUE(harness.sentFor(net::MessageType::objectComponentsChanged, live->getUUID()));
}

TEST(StructuralChanges, SpawnsAreBroadcastWhilePendingAndAreMembersAfter)
{
  const auto scene = makeScene();
  addObject(scene, "Existing");

  Harness harness;
  const auto spawned = std::make_shared<Object>("Spawned");
  {
    const ObjectManager::ScriptPassGuard guard(*scene.objectManager);
    scene.objectManager->addObject(spawned);
  }
  harness.spawned.push_back(spawned);
  ASSERT_FALSE(isMember(scene, spawned));

  bool memberAtBroadcast = true;
  harness.onBroadcast = [&](const net::Message& message)
  {
    if (message.getType() == net::MessageType::objectSpawned)
    {
      memberAtBroadcast = isMember(scene, spawned);
    }
  };

  harness.run(scene.objectManager.get());

  EXPECT_TRUE(harness.sentFor(net::MessageType::objectSpawned, spawned->getUUID()));
  EXPECT_FALSE(memberAtBroadcast);
  EXPECT_TRUE(isMember(scene, spawned));
  EXPECT_EQ(harness.notifiedSpawned, std::vector<uuids::uuid>{spawned->getUUID()});
}

TEST(StructuralChanges, SpawnedThenDestroyedIsBroadcastButNotReportedAndIsGone)
{
  const auto scene = makeScene();
  const auto keeper = std::make_shared<Object>("Keeper");
  const auto fleeting = std::make_shared<Object>("Fleeting");
  {
    const ObjectManager::ScriptPassGuard guard(*scene.objectManager);
    scene.objectManager->addObject(keeper);
    scene.objectManager->addObject(fleeting);
    scene.objectManager->removeObject(fleeting);
  }

  Harness harness;
  harness.spawned = {keeper, fleeting};
  harness.destroyed.push_back(fleeting->getUUID());

  harness.run(scene.objectManager.get());

  EXPECT_TRUE(harness.sentFor(net::MessageType::objectSpawned, fleeting->getUUID()));
  EXPECT_TRUE(harness.sentFor(net::MessageType::objectDestroyed, fleeting->getUUID()));
  EXPECT_EQ(scene.objectManager->getObjectByUUID(fleeting->getUUID()), nullptr);
  EXPECT_FALSE(isMember(scene, fleeting));

  // Positive control: the spawn that survived is reported.
  EXPECT_TRUE(contains(harness.notifiedSpawned, keeper->getUUID()));
  EXPECT_FALSE(contains(harness.notifiedSpawned, fleeting->getUUID()));
  EXPECT_EQ(harness.notifiedDestroyed, std::vector<uuids::uuid>{fleeting->getUUID()});
}

TEST(StructuralChanges, ASpawnedParentReportsItsChildrenToo)
{
  const auto scene = makeScene();
  const auto parent = std::make_shared<Object>("Parent");
  const auto child = std::make_shared<Object>("Child");
  {
    const ObjectManager::ScriptPassGuard guard(*scene.objectManager);
    scene.objectManager->addObject(parent);
    child->setParent(parent);
    scene.objectManager->addObject(child);
  }

  Harness harness;
  harness.spawned.push_back(parent);

  harness.run(scene.objectManager.get());

  const std::vector<uuids::uuid> expected = {parent->getUUID(), child->getUUID()};
  EXPECT_EQ(harness.notifiedSpawned, expected);
  EXPECT_TRUE(isMember(scene, child));
}

TEST(StructuralChanges, ADestroyedObjectInTheChangedBufferIsNotResynced)
{
  const auto scene = makeScene();
  const auto live = addObject(scene, "Live");
  const auto doomed = addObject(scene, "Doomed");
  scene.objectManager->removeObject(doomed);

  Harness harness;
  harness.destroyed.push_back(doomed->getUUID());
  harness.changed = {doomed->getUUID(), live->getUUID()};

  harness.run(scene.objectManager.get());

  EXPECT_TRUE(harness.sentFor(net::MessageType::objectComponentsChanged, live->getUUID()));
  EXPECT_FALSE(harness.sentFor(net::MessageType::objectComponentsChanged, doomed->getUUID()));
}

TEST(StructuralChanges, AChangeRecordedByAHandlerIsBroadcastInTheSameCall)
{
  const auto scene = makeScene();
  const auto target = addObject(scene, "Target");

  Harness harness;
  harness.onNotify = [&] { harness.changed.push_back(target->getUUID()); };

  harness.run(scene.objectManager.get());

  EXPECT_TRUE(harness.sentFor(net::MessageType::objectComponentsChanged, target->getUUID()));
}

TEST(StructuralChanges, ASpawnByAHandlerWaitsForTheNextCall)
{
  const auto scene = makeScene();
  const auto late = std::make_shared<Object>("Late");

  Harness harness;
  harness.onNotify = [&]
  {
    const ObjectManager::ScriptPassGuard guard(*scene.objectManager);
    scene.objectManager->addObject(late);
    harness.spawned.push_back(late);
  };

  harness.run(scene.objectManager.get());
  EXPECT_FALSE(harness.sentFor(net::MessageType::objectSpawned, late->getUUID()));
  EXPECT_FALSE(isMember(scene, late));

  harness.onNotify = nullptr;
  harness.run(scene.objectManager.get());
  EXPECT_TRUE(harness.sentFor(net::MessageType::objectSpawned, late->getUUID()));
  EXPECT_TRUE(isMember(scene, late));
  EXPECT_EQ(harness.notifiedSpawned, std::vector<uuids::uuid>{late->getUUID()});
}

TEST(StructuralChanges, WithoutASceneTheBroadcastsGoOutAndNothingIsReported)
{
  const auto spawned = std::make_shared<Object>("Spawned");
  const auto gone = uuids::uuid::from_string("00000000-0000-0000-0000-0000000000a1").value();
  const auto changed = uuids::uuid::from_string("00000000-0000-0000-0000-0000000000a2").value();

  Harness harness;
  harness.spawned.push_back(spawned);
  harness.destroyed.push_back(gone);
  harness.changed.push_back(changed);

  harness.run(nullptr);

  const std::vector<net::MessageType> expected = {net::MessageType::objectSpawned, net::MessageType::objectDestroyed};
  EXPECT_EQ(harness.sentTypes(), expected);
  EXPECT_TRUE(harness.notifiedSpawned.empty());
  EXPECT_EQ(harness.notifiedDestroyed, std::vector<uuids::uuid>{gone});
  EXPECT_EQ(harness.events.back(), "takeChanged");
}
