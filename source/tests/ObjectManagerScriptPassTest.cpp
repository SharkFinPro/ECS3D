#include <gtest/gtest.h>

#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "ObjectManagerFixtures.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {
  using namespace objectManagerFixtures;
}

// ScriptSystem::fixedUpdate/variableUpdate range over getAllObjects() and run script code (which can call
// World.spawn/spawnPrefab) inside the loop - a live reference to m_allObjects, so an addObject that
// appended straight to it could reallocate the vector out from under that range-for. ObjectManager defers
// the append instead while a ScriptPassGuard is alive, exactly like removeObject already defers to
// deleteObjectsMarkedForDeletion; these tests drive that guard directly (ScriptSystem needs the CoreCLR
// host the rest of the suite stays free of).
TEST(ObjectManager, ObjectsAddedDuringAScriptPassDoNotJoinTheListUntilFlushed)
{
  const auto scene = makeScene();
  const auto original = addObject(scene, "Original");

  {
    const ObjectManager::ScriptPassGuard guard(*scene.objectManager);

    const auto spawned = std::make_shared<Object>("Spawned");
    scene.objectManager->addObject(spawned);

    // Deferred: the pass is still "in progress" (the guard is alive), so the object must not be a member
    // of the list a concurrent range-for over getAllObjects() would be iterating.
    EXPECT_EQ(scene.objectManager->getAllObjects().size(), 1u);
    EXPECT_EQ(scene.objectManager->getObjects().size(), 1u);

    // But still immediately usable: a script positioning/looking up the object it just spawned must not
    // have to wait for the flush.
    EXPECT_EQ(scene.objectManager->getObjectByUUID(spawned->getUUID()), spawned);

    // getPendingAdditions() is what a World binding that scans every object (findObjectByName,
    // getAllObjectUuids) has to check too, alongside getAllObjects() - otherwise it disagrees with
    // getObjectByUUID/objectExists about whether this object exists yet.
    ASSERT_EQ(scene.objectManager->getPendingAdditions().size(), 1u);
    EXPECT_EQ(scene.objectManager->getPendingAdditions().front(), spawned);
  }

  scene.objectManager->flushPendingAdditions();

  ASSERT_EQ(scene.objectManager->getAllObjects().size(), 2u);
  ASSERT_EQ(scene.objectManager->getObjects().size(), 2u);
  EXPECT_EQ(scene.objectManager->getObjects().back()->getName(), "Spawned");
  EXPECT_TRUE(scene.objectManager->getPendingAdditions().empty());

  // Positive control: the object that was already there the whole time is untouched by any of this.
  EXPECT_NE(std::ranges::find(scene.objectManager->getAllObjects(), original),
            scene.objectManager->getAllObjects().end());
}

// The exact contract WorldBindings::bindFindObjectByName/bindGetAllObjectUuids rely on: a caller that
// wants a consistent view of "every object that exists right now" during a script pass has to scan
// getAllObjects() and getPendingAdditions() together, not getAllObjects() alone - getObjectByUUID already
// does both (see above), and a name/uuid-listing binding that only checked getAllObjects() would disagree
// with it about an object a script spawned earlier in the very same pass.
TEST(ObjectManager, FindingAnObjectByNameDuringAScriptPassRequiresCheckingBothListsLikeUuidLookupAlreadyDoes)
{
  const auto scene = makeScene();

  const ObjectManager::ScriptPassGuard guard(*scene.objectManager);

  const auto spawned = std::make_shared<Object>("SpawnedByScript");
  scene.objectManager->addObject(spawned);

  auto findByName = [&](const std::string& name) -> std::shared_ptr<Object> {
    for (const auto& object : scene.objectManager->getAllObjects())
    {
      if (object->getName() == name)
      {
        return object;
      }
    }
    for (const auto& object : scene.objectManager->getPendingAdditions())
    {
      if (object->getName() == name)
      {
        return object;
      }
    }
    return nullptr;
  };

  EXPECT_EQ(findByName("SpawnedByScript"), spawned);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(spawned->getUUID()), spawned);
}

// The actual bug: appending to m_allObjects while a range-for over it is in progress can reallocate the
// vector and invalidate every iterator the loop holds - a range-for's begin()/end() are captured once at
// entry, so that reallocation leaves them dangling, UB for the rest of the pass. This uses the exact same
// loop shape ScriptSystem::fixedUpdate/variableUpdate use (a plain range-for over getAllObjects(), not an
// index re-reading size() each step - that shape would instead just hang, growing its own bound by
// exactly the same amount as the index every iteration, which would demonstrate a different bug than the
// one being fixed here). The adds are bounded by the original count precisely so this terminates either
// way: on the fixed code nothing is added to the vector the range-for holds iterators into, so it visits
// the originals and stops; on a hypothetically unguarded addObject, the range-for's cached end() would
// still only cover the originals captured at entry (a reallocation invalidates iterators, it does not
// extend how many the loop was ever going to visit), so an unbounded add-per-visit was never needed to
// make this terminate - it would just make a real defect harder to tell apart from a runaway loop.
TEST(ObjectManager, SpawningOncePerVisitDuringARangeForPassLeavesTheListUnchangedUntilFlush)
{
  const auto scene = makeScene();

  std::vector<std::shared_ptr<Object>> originals;
  for (int i = 0; i < 8; ++i)
  {
    originals.push_back(addObject(scene, "Original" + std::to_string(i)));
  }

  std::size_t visitCount = 0;
  {
    const ObjectManager::ScriptPassGuard guard(*scene.objectManager);

    for (const auto& object : scene.objectManager->getAllObjects())
    {
      (void)object;
      ++visitCount;

      scene.objectManager->addObject(std::make_shared<Object>("Spawned" + std::to_string(visitCount)));

      // Deferred: the list this range-for is holding iterators into must not change size mid-pass, or a
      // real (unguarded) addObject reallocating it out from under the loop is exactly the bug being fixed.
      EXPECT_EQ(scene.objectManager->getAllObjects().size(), originals.size());
    }
  }

  // Every original was visited exactly once - the guarantee a reallocating vector mid range-for would not
  // honor (a moved/reallocated backing store can skip or repeat elements, or worse).
  EXPECT_EQ(visitCount, originals.size());
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), originals.size());

  scene.objectManager->flushPendingAdditions();

  // All new objects are present after flush - deferring them did not drop any.
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), originals.size() * 2);
  for (const auto& original : originals)
  {
    EXPECT_NE(std::ranges::find(scene.objectManager->getAllObjects(), original),
              scene.objectManager->getAllObjects().end());
  }
}

TEST(ObjectManager, SpawningThenDestroyingInTheSamePassRemovesTheObjectOnceBothFlush)
{
  const auto scene = makeScene();

  std::shared_ptr<Object> spawned;
  {
    const ObjectManager::ScriptPassGuard guard(*scene.objectManager);

    spawned = std::make_shared<Object>("Spawned");
    scene.objectManager->addObject(spawned);

    // Destroying it in the very same pass (World.spawnObject immediately followed by World.destroyObject)
    // has to find it via getObjectByUUID while it is still only pending.
    const auto found = scene.objectManager->getObjectByUUID(spawned->getUUID());
    ASSERT_EQ(found, spawned);
    EXPECT_TRUE(scene.objectManager->removeObject(found));
  }

  // Flush order matters: additions have to land before the delete pass runs, or there is nothing there
  // for it to find and remove.
  scene.objectManager->flushPendingAdditions();
  scene.objectManager->deleteObjectsMarkedForDeletion();

  EXPECT_EQ(scene.objectManager->getObjectByUUID(spawned->getUUID()), nullptr);
  EXPECT_TRUE(scene.objectManager->getAllObjects().empty());
  EXPECT_TRUE(scene.objectManager->getObjects().empty());
}

// A prefab spawned mid-pass (World.spawnPrefab) registers its root, then every child, all through
// addObject - each should defer and land in the same relative order once flushed.
TEST(ObjectManager, ASpawnedPrefabsChildJoinsAlongsideItsRootOnFlush)
{
  const auto scene = makeScene();

  std::shared_ptr<Object> root;
  std::shared_ptr<Object> child;
  {
    const ObjectManager::ScriptPassGuard guard(*scene.objectManager);

    root = std::make_shared<Object>("Root");
    scene.objectManager->addObject(root);

    child = std::make_shared<Object>("Child");
    child->setParent(root);
    scene.objectManager->addObject(child);

    // Parenting isn't deferred - only flat-list membership is - so the child is already reachable from its
    // root during the pass, the way instantiateUnder's own tree-building depends on.
    EXPECT_EQ(root->getChildren().size(), 1u);
    EXPECT_EQ(root->getChildren().front(), child);

    EXPECT_TRUE(scene.objectManager->getAllObjects().empty());
  }

  scene.objectManager->flushPendingAdditions();

  ASSERT_EQ(scene.objectManager->getObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getObjects().front(), root);
  ASSERT_EQ(scene.objectManager->getAllObjects().size(), 2u);
  EXPECT_EQ(child->getParent(), root);
}

// Non-script callers (editor edits, snapshot unpack, Object::unpack reconciliation) never run inside a
// ScriptPassGuard, so they must keep the old immediate semantics - the positive control proving the guard,
// not addObject itself, is what changed.
TEST(ObjectManager, AddObjectOutsideAScriptPassIsStillImmediate)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Immediate");

  EXPECT_EQ(scene.objectManager->getAllObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(object->getUUID()), object);
}
