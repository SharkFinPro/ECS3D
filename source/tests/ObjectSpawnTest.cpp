#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"

#include <Protocol.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace {
  struct Scene {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    std::unique_ptr<ObjectManager> objectManager;
  };

  Scene makeScene()
  {
    Scene scene;
    registerDataComponents(*scene.componentRegistry);
    scene.objectManager = std::make_unique<ObjectManager>(scene.componentRegistry);

    return scene;
  }

  // A parent with one child, packed the way the server broadcasts a runtime spawn.
  net::Message packedSubtree(const Scene& source)
  {
    const auto parent = std::make_shared<Object>("Spawned");
    source.objectManager->addObject(parent);

    const auto child = std::make_shared<Object>("Spawned Child");
    child->setParent(parent);
    source.objectManager->addObject(child);

    return replication::buildObjectSpawned(*parent);
  }

  net::Message firstBytes(const net::Message& source, const std::size_t kept)
  {
    net::Message result(net::MessageType::objectSpawned);

    const auto bytes = source.bytes();
    for (std::size_t i = 0; i < kept && i < bytes.size(); ++i)
    {
      result.write(bytes[i]);
    }

    return result;
  }
}

TEST(ObjectSpawn, SplicesTheWholeSubtreeIntoTheScene)
{
  const auto source = makeScene();
  const auto message = packedSubtree(source);

  const auto target = makeScene();
  replication::applyObjectSpawned(*target.objectManager, message);

  ASSERT_EQ(target.objectManager->getObjects().size(), 1u);
  EXPECT_EQ(target.objectManager->getAllObjects().size(), 2u);

  const auto spawned = target.objectManager->getObjects().front();
  EXPECT_EQ(spawned->getName(), "Spawned");
  ASSERT_EQ(spawned->getChildren().size(), 1u);
  EXPECT_EQ(spawned->getChildren().front()->getName(), "Spawned Child");
}

TEST(ObjectSpawn, LeavesNothingBehindWhenTheRootFailsToUnpack)
{
  const auto source = makeScene();
  const auto message = packedSubtree(source);

  const auto target = makeScene();

  const auto resident = std::make_shared<Object>("Resident");
  target.objectManager->addObject(resident);

  // The uuid and the name's length prefix survive, the name itself does not, so the root throws before
  // it reaches its components. The object was already registered by then.
  const std::size_t upToTheName = sizeof(uint32_t) + 36 + sizeof(uint32_t);
  EXPECT_ANY_THROW(replication::applyObjectSpawned(*target.objectManager,
                                                   firstBytes(message, upToTheName)));

  EXPECT_EQ(target.objectManager->getObjects().size(), 1u);
  EXPECT_EQ(target.objectManager->getAllObjects().size(), 1u);
  EXPECT_EQ(target.objectManager->getObjects().front(), resident);
}

TEST(ObjectSpawn, LeavesNothingBehindWhenAChildFailsToUnpack)
{
  const auto source = makeScene();
  const auto message = packedSubtree(source);

  const auto target = makeScene();

  const auto resident = std::make_shared<Object>("Resident");
  target.objectManager->addObject(resident);

  // The root unpacks, registers its child, and the child then runs out - so the unwind has to reach
  // past the object it started from. Without it the tree keeps a phantom parent and child.
  EXPECT_ANY_THROW(replication::applyObjectSpawned(*target.objectManager,
                                                   firstBytes(message, message.size() - 4)));

  EXPECT_EQ(target.objectManager->getObjects().size(), 1u);
  EXPECT_EQ(target.objectManager->getAllObjects().size(), 1u);
  EXPECT_EQ(target.objectManager->getObjects().front(), resident);
}

TEST(ObjectSpawn, DiscardingASubtreeDetachesItFromALiveParent)
{
  const auto scene = makeScene();

  const auto keep = std::make_shared<Object>("Keep");
  scene.objectManager->addObject(keep);

  const auto doomed = std::make_shared<Object>("Doomed");
  doomed->setParent(keep);
  scene.objectManager->addObject(doomed);

  const auto grandchild = std::make_shared<Object>("Grandchild");
  grandchild->setParent(doomed);
  scene.objectManager->addObject(grandchild);

  scene.objectManager->discardSubtree(doomed);

  // The parent survives the discard, so it has to lose its reference to the subtree as well - otherwise
  // the tree still walks into objects the manager no longer knows about.
  EXPECT_TRUE(keep->getChildren().empty());
  EXPECT_EQ(scene.objectManager->getObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getAllObjects().front(), keep);
}
