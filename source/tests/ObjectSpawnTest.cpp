#include <gtest/gtest.h>

#include "TestScene.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/collisions/Collider.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <Protocol.h>
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
  using fixtures::makeScene;
  using fixtures::Scene;

  // A parent with one child, packed the way the server broadcasts a runtime spawn.
  net::Message packedSubtree(const Scene& source)
  {
    const auto parent = addObject(source, "Spawned");
    addChildObject(source, "Spawned Child", parent);

    return replication::buildObjectSpawned(*parent);
  }

  // A root with `levels` single-child descendants below it (root -> child -> ... -> levels deep), built
  // through the same registry-backed factories a loaded scene uses. Returns the root, whose own
  // serialize()/pack() then carries the whole chain - a loop, not recursion, so building a deep fixture
  // doesn't itself risk the stack the fix is protecting.
  std::shared_ptr<Object> chainOfDepth(const Scene& scene, const std::size_t levels)
  {
    const auto root = addObject(scene, "Root");

    auto current = root;
    for (std::size_t i = 0; i < levels; ++i)
    {
      current = addChildObject(scene, "Descendant", current);
    }

    return root;
  }

  // The node `levels` steps below start, built through addChildObject the same way chainOfDepth is -
  // but returning the deepest node instead of the root, for tests that need a parent sitting at a
  // specific depth rather than a whole chain to serialize.
  std::shared_ptr<Object> descendantAtDepth(const Scene& scene, const std::shared_ptr<Object>& start,
                                            const std::size_t levels)
  {
    auto current = start;
    for (std::size_t i = 0; i < levels; ++i)
    {
      current = addChildObject(scene, "Descendant", current);
    }

    return current;
  }

  // A well-formed object header claiming one component, followed by nothing but that component's
  // discriminator - enough to reach the check without needing a body the check should never get to.
  net::Message objectWithOneComponentTag(const ComponentType packedType)
  {
    net::Message message(net::MessageType::objectSpawned);
    message.writeString("123e4567-e89b-12d3-a456-426614174000");
    message.writeString("Spawned");
    message.write<uint32_t>(1);
    message.write(packedType);

    return message;
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

TEST(ObjectSpawn, RefusesAPackedComponentTypeThatNamesNoComponent)
{
  const auto target = makeScene();

  // A well-formed object header followed by a discriminator that is no component type. This threw
  // before too, out of componentTypeToRegistryKey.at() - the exception type is what distinguishes the
  // check from the incidental lookup failure it replaces.
  EXPECT_THROW(replication::applyObjectSpawned(*target.objectManager,
                                               objectWithOneComponentTag(static_cast<ComponentType>(0x7F))),
               std::runtime_error);
  EXPECT_TRUE(target.objectManager->getAllObjects().empty());
}

TEST(ObjectSpawn, RefusesAScriptPackedAmongTheComponents)
{
  const auto target = makeScene();

  // script is a real component type, so it passes the registry-key check - but Object::pack never writes
  // it in the component section. Left through, it built a nameless Script and then read the next
  // component's bytes as its field blob.
  EXPECT_THROW(replication::applyObjectSpawned(*target.objectManager,
                                               objectWithOneComponentTag(ComponentType::script)),
               std::runtime_error);
  EXPECT_TRUE(target.objectManager->getAllObjects().empty());
}

TEST(ObjectSpawn, RefusesAComponentTypeThisBuildDoesNotRegister)
{
  // A registry with nothing registered in it: every key is valid, none of them can be created. This is
  // the case that used to be a null dereference rather than an exception - create() returns null and
  // addComponent reads getType() off it, which no guard around this path can catch. ModelRenderer is a
  // type this build does register, so the empty registry is the whole test: with the components in it
  // the payload would get as far as unpacking a body that is not there and throw for another reason.
  Scene target(fixtures::Components::none);

  ASSERT_EQ(target.componentRegistry->create("ModelRenderer"), nullptr);

  EXPECT_THROW(replication::applyObjectSpawned(*target.objectManager,
                                               objectWithOneComponentTag(ComponentType::modelRenderer)),
               std::runtime_error);
  EXPECT_TRUE(target.objectManager->getAllObjects().empty());
}

TEST(ObjectSpawn, RebuildsAColliderSlotWhenThePayloadIsTheOtherShape)
{
  const auto source = makeScene();

  const auto packed = std::make_shared<Object>("Spawned");
  source.objectManager->addObject(packed);
  packed->addComponent(std::make_shared<BoxCollider>());

  net::Message message(net::MessageType::objectSpawned);
  packed->pack(message);

  const auto target = makeScene();

  const auto existing = std::make_shared<Object>("Existing");
  target.objectManager->addObject(existing);
  existing->addComponent(std::make_shared<SphereCollider>());

  net::MessageReader reader(message);
  existing->unpack(reader);

  // Both shapes are stored under the collider key, so reusing the slot would have read a box body into
  // the sphere - a 12-byte vector where a 4-byte float belongs, and every field after it misaligned.
  const auto collider = existing->getComponent<Collider>(ComponentType::collider);
  ASSERT_NE(collider, nullptr);
  EXPECT_EQ(collider->getPackedType(), ComponentType::SubComponentType_boxCollider);
  EXPECT_EQ(reader.remaining(), 0u);
}

TEST(ObjectSpawn, UnpacksAChainExactlyAtTheDepthLimit)
{
  const auto source = makeScene();
  const auto root = chainOfDepth(source, maxObjectDepth);
  const auto message = replication::buildObjectSpawned(*root);

  const auto target = makeScene();
  replication::applyObjectSpawned(*target.objectManager, message);

  // root plus one descendant per level.
  EXPECT_EQ(target.objectManager->getAllObjects().size(), maxObjectDepth + 1);
}

TEST(ObjectSpawn, RefusesAChainOneLevelDeeperThanTheDepthLimitAndLeavesNothingBehind)
{
  const auto source = makeScene();
  const auto root = chainOfDepth(source, maxObjectDepth + 1);
  const auto message = replication::buildObjectSpawned(*root);

  const auto target = makeScene();

  // Paired with UnpacksAChainExactlyAtTheDepthLimit above: that test is the positive control proving a
  // chain this shape unpacks at all, so a throw here is the depth check firing, not some other defect.
  EXPECT_THROW(replication::applyObjectSpawned(*target.objectManager, message), std::runtime_error);
  EXPECT_TRUE(target.objectManager->getAllObjects().empty());
}

TEST(ObjectSpawn, InstantiatesAJsonChainExactlyAtTheDepthLimit)
{
  const auto source = makeScene();
  const auto root = chainOfDepth(source, maxObjectDepth);
  const auto body = root->serialize();

  const auto target = makeScene();
  target.objectManager->instantiate(body);

  EXPECT_EQ(target.objectManager->getAllObjects().size(), maxObjectDepth + 1);
}

TEST(ObjectSpawn, RefusesAJsonChainOneLevelDeeperThanTheDepthLimitAndLeavesNothingBehind)
{
  const auto source = makeScene();
  const auto root = chainOfDepth(source, maxObjectDepth + 1);
  const auto body = root->serialize();

  const auto target = makeScene();

  // Paired with InstantiatesAJsonChainExactlyAtTheDepthLimit above, the same way the wire pair is.
  EXPECT_THROW(target.objectManager->instantiate(body), std::runtime_error);
  EXPECT_TRUE(target.objectManager->getAllObjects().empty());
}

TEST(ObjectSpawn, DuplicatesALeafBodyUnderAParentOneBelowTheDepthLimit)
{
  const auto scene = makeScene();

  // duplicateObject is instantiateUnder's other caller, landing its copy beside the source - under the
  // source's own live parent, not under the source itself - so a source sitting at maxObjectDepth with
  // no children of its own is a body that is only 1 node deep dropped under a parent one below the limit.
  const auto deepParent = descendantAtDepth(scene, addObject(scene, "Root"), maxObjectDepth - 1);
  const auto source = addChildObject(scene, "Source", deepParent);

  const auto before = scene.objectManager->getAllObjects().size();
  scene.objectManager->duplicateObject(source);

  // The copy lands at maxObjectDepth exactly - the positive control proving this shape of duplicate
  // applies at all, so the throw below is the depth check firing and not some other defect.
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before + 1);
}

TEST(ObjectSpawn, DuplicatingATwoLevelBodyPastTheDepthLimitThrowsAndLeavesNothingBehind)
{
  const auto scene = makeScene();

  // One level deeper than the positive control above: source now carries a child of its own, so its
  // duplicate (root at maxObjectDepth, child at maxObjectDepth + 1) no longer fits.
  const auto deepParent = descendantAtDepth(scene, addObject(scene, "Root"), maxObjectDepth - 1);
  const auto source = addChildObject(scene, "Source", deepParent);
  addChildObject(scene, "SourceChild", source);

  const auto before = scene.objectManager->getAllObjects().size();

  EXPECT_THROW(scene.objectManager->duplicateObject(source), std::runtime_error);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);
}
