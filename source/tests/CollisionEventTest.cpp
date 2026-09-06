#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "CollisionSystem.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/Collider.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <memory>
#include <ostream>
#include <string>
#include <vector>
#include <uuid.h>

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

  // Whether the object gets a RigidBody decides whether it is a collision *source*: the sweep skips an
  // edge with no rigid body entirely, so a pair of static colliders is never even tested.
  std::shared_ptr<Object> addBody(const Scene& scene, const std::string& name, const glm::vec3& position,
                                  const bool dynamic, const bool trigger = false)
  {
    auto object = std::make_shared<Object>(name);
    scene.objectManager->addObject(object);

    object->getComponent<Transform>(ComponentType::transform)->setPosition(position);

    const auto collider = std::make_shared<BoxCollider>();
    object->addComponent(collider);
    collider->setIsTrigger(trigger);

    if (dynamic)
    {
      object->addComponent(std::make_shared<RigidBody>());
    }

    return object;
  }

  bool contains(const std::vector<CollisionPair>& pairs, const std::shared_ptr<Object>& a,
                const std::shared_ptr<Object>& b)
  {
    return std::ranges::find(pairs, CollisionPair::make(a->getUUID(), b->getUUID())) != pairs.end();
  }

  glm::vec3 positionOf(const std::shared_ptr<Object>& object)
  {
    return object->getComponent<Transform>(ComponentType::transform)->getPosition();
  }
}

// Found by ADL: CollisionPair is a global type, so this has to be too. Without it a failed comparison
// is thirty-two bytes of hex.
inline void PrintTo(const CollisionPair& pair, std::ostream* out)
{
  *out << "{" << uuids::to_string(pair.a) << ", " << uuids::to_string(pair.b) << "}";
}

TEST(CollisionPairOrdering, IsTheSamePairWhicheverWayRoundItIsBuilt)
{
  const auto scene = makeScene();
  const auto a = addBody(scene, "A", { 0, 0, 0 }, true);
  const auto b = addBody(scene, "B", { 5, 0, 0 }, true);

  // A contact between two dynamic bodies is found from both sides, so the canonical form is what stops
  // one contact being reported as two.
  EXPECT_EQ(CollisionPair::make(a->getUUID(), b->getUUID()),
            CollisionPair::make(b->getUUID(), a->getUUID()));

  const auto pair = CollisionPair::make(a->getUUID(), b->getUUID());
  EXPECT_LT(pair.a, pair.b);
}

TEST(CollisionPairOrdering, OrdersLexicographicallyByBothMembers)
{
  const auto scene = makeScene();
  const auto first = addBody(scene, "A", { 0, 0, 0 }, true);
  const auto second = addBody(scene, "B", { 5, 0, 0 }, true);
  const auto third = addBody(scene, "C", { 10, 0, 0 }, true);

  // Sorted with an explicit comparator rather than through std::ranges: uuid has < and ==, but not the
  // full relational set that ranges::sort's sortable concept asks for. That gap is exactly why
  // CollisionPair spells out its own three-way comparison instead of inheriting one.
  std::vector sorted{ first->getUUID(), second->getUUID(), third->getUUID() };
  std::sort(sorted.begin(), sorted.end(),
            [](const uuids::uuid& a, const uuids::uuid& b) { return a < b; });

  // The event lists are produced by set_difference and set_intersection, which need a total order and
  // not just equality. Comparing on the second member as well as the first is what makes it one.
  const auto low = CollisionPair::make(sorted[0], sorted[1]);
  const auto high = CollisionPair::make(sorted[0], sorted[2]);

  EXPECT_LT(low, high);
  EXPECT_GT(high, low);
  EXPECT_NE(low, high);
}

TEST(CollisionEvent, AContactEntersThenStaysThenExits)
{
  const auto scene = makeScene();

  // Triggers on purpose: a solid contact is pushed apart by the response in the same tick that reports
  // it, so the only way to hold two bodies overlapping across ticks is to ask for no response.
  //
  // It also keeps this file to at most one *solid* dynamic body per scene, which is deliberate. The
  // narrow phase runs in an OpenMP loop over the edges, and a collider's bounding box and transformed
  // mesh are caches it writes on read. They are only safe to share because checkCollisions warms them
  // serially first, which stops being true the moment a response moves something mid-loop.
  const auto moving = addBody(scene, "Moving", { 0, 0, 0 }, true, true);
  const auto resting = addBody(scene, "Resting", { 1, 0, 0 }, false, true);

  CollisionSystem collisionSystem;

  collisionSystem.fixedUpdate(*scene.objectManager);
  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), moving, resting));
  EXPECT_TRUE(collisionSystem.getCollisionStays().empty());
  EXPECT_TRUE(collisionSystem.getCollisionExits().empty());

  // Nothing moved, so the same contact is a stay rather than a second enter.
  collisionSystem.fixedUpdate(*scene.objectManager);
  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());
  EXPECT_TRUE(contains(collisionSystem.getCollisionStays(), moving, resting));
  EXPECT_TRUE(collisionSystem.getCollisionExits().empty());

  moving->getComponent<Transform>(ComponentType::transform)->setPosition({ 50, 0, 0 });

  collisionSystem.fixedUpdate(*scene.objectManager);
  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());
  EXPECT_TRUE(collisionSystem.getCollisionStays().empty());
  EXPECT_TRUE(contains(collisionSystem.getCollisionExits(), moving, resting));

  // And the exit is reported once, not every tick after it.
  collisionSystem.fixedUpdate(*scene.objectManager);
  EXPECT_TRUE(collisionSystem.getCollisionExits().empty());
}

TEST(CollisionEvent, ObjectsThatNeverTouchProduceNothing)
{
  const auto scene = makeScene();
  const auto a = addBody(scene, "A", { 0, 0, 0 }, true, true);
  const auto b = addBody(scene, "B", { 50, 0, 0 }, true, true);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager);

  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());
  EXPECT_TRUE(collisionSystem.getCollisionStays().empty());
  EXPECT_TRUE(collisionSystem.getCollisionExits().empty());

  // Moved into contact, so the silence above is the distance and not a narrow phase that answers no to
  // everything. Every absence assertion in this file carries one of these.
  b->getComponent<Transform>(ComponentType::transform)->setPosition({ 1, 0, 0 });
  collisionSystem.fixedUpdate(*scene.objectManager);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), a, b));
}

TEST(CollisionEvent, APairSeenFromBothSidesIsRecordedOnce)
{
  const auto scene = makeScene();
  const auto a = addBody(scene, "A", { 0, 0, 0 }, true, true);
  const auto b = addBody(scene, "B", { 1, 0, 0 }, true, true);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager);

  // Both are dynamic, so both edges find the contact. Scripts would otherwise get onCollisionEnter twice
  // for one collision.
  EXPECT_EQ(collisionSystem.getCollisionEnters().size(), 1u);
  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), a, b));
}

TEST(CollisionEvent, AStaticPairIsNeverEvenTested)
{
  const auto scene = makeScene();
  const auto a = addBody(scene, "A", { 0, 0, 0 }, false, true);
  const auto b = addBody(scene, "B", { 1, 0, 0 }, false, true);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager);

  // Overlapping, but neither is a collision source: the sweep skips an edge with no rigid body, so two
  // pieces of static geometry sharing a space cost nothing and report nothing.
  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());

  // The same overlap, with one of them given a body. This is what makes the silence above about the
  // missing body rather than about the boxes not touching.
  a->addComponent(std::make_shared<RigidBody>());
  collisionSystem.fixedUpdate(*scene.objectManager);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), a, b));
}

TEST(CollisionEvent, LayersThatDoNotShareAMaskProduceNoEvent)
{
  const auto scene = makeScene();
  const auto a = addBody(scene, "A", { 0, 0, 0 }, true, true);
  const auto b = addBody(scene, "B", { 1, 0, 0 }, true, true);

  a->getComponent<Collider>(ComponentType::collider)->setLayer(1);
  b->getComponent<Collider>(ComponentType::collider)->setLayer(2);
  a->getComponent<Collider>(ComponentType::collider)->setMask(1u << 1);
  b->getComponent<Collider>(ComponentType::collider)->setMask(1u << 2);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager);

  // The filter sits ahead of the narrow phase, so a filtered pair produces neither a response nor an
  // event - not an event without a response, which is what a trigger is.
  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());

  // Each mask now admits the other's layer, and the same overlap becomes a contact.
  a->getComponent<Collider>(ComponentType::collider)->setMask(1u << 2);
  b->getComponent<Collider>(ComponentType::collider)->setMask(1u << 1);

  collisionSystem.fixedUpdate(*scene.objectManager);
  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), a, b));
}

TEST(CollisionEvent, OneSidedMaskAgreementIsNotEnough)
{
  const auto scene = makeScene();
  const auto a = addBody(scene, "A", { 0, 0, 0 }, true, true);
  const auto b = addBody(scene, "B", { 1, 0, 0 }, true, true);

  a->getComponent<Collider>(ComponentType::collider)->setLayer(1);
  b->getComponent<Collider>(ComponentType::collider)->setLayer(2);

  // Pinned, because the rest of this test reads the same whether the layers are 1 and 2 or both 0 -
  // a setLayer that stopped working would leave it green.
  ASSERT_EQ(a->getComponent<Collider>(ComponentType::collider)->getLayer(), 1u);
  ASSERT_EQ(b->getComponent<Collider>(ComponentType::collider)->getLayer(), 2u);

  // A admits B's layer; B does not admit A's. The rule is that both have to agree, so this is no
  // contact rather than a contact one side does not know about.
  a->getComponent<Collider>(ComponentType::collider)->setMask(1u << 2);
  b->getComponent<Collider>(ComponentType::collider)->setMask(1u << 2);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager);

  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());

  // B admitting A as well turns the same overlap into a contact, so the refusal above is the one-sided
  // mask and not the geometry.
  b->getComponent<Collider>(ComponentType::collider)->setMask(1u << 1);
  collisionSystem.fixedUpdate(*scene.objectManager);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), a, b));
}

TEST(CollisionEvent, AParentAndItsChildDoNotCollide)
{
  const auto scene = makeScene();

  auto parent = std::make_shared<Object>("Parent");
  scene.objectManager->addObject(parent);
  parent->getComponent<Transform>(ComponentType::transform)->setPosition({ 0, 0, 0 });
  parent->addComponent(std::make_shared<BoxCollider>());
  parent->addComponent(std::make_shared<RigidBody>());

  auto child = std::make_shared<Object>("Child");
  child->setParent(parent);
  scene.objectManager->addObject(child);
  child->getComponent<Transform>(ComponentType::transform)->setPosition({ 0, 0, 0 });
  child->addComponent(std::make_shared<BoxCollider>());

  // The skip has to hold from both edges, and the child's edge only exists because getComponent walks
  // to the parent for a rigid body. Without this the child would be no collision source at all, the
  // parent's edge would carry the whole test, and deleting the child-side skip would leave it green.
  ASSERT_NE(child->getComponent<RigidBody>(ComponentType::rigidBody), nullptr);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager);

  // A child's transform is relative to its parent, so the two occupy the same space by construction.
  // Reporting that as a contact would fire an event on every tick of every composed object.
  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());

  // Detached, and the very same overlap is a contact. Without this the test passes just as well against
  // a narrow phase that answers no to everything.
  child->setParent(nullptr);
  parent->removeChild(child);
  scene.objectManager->addObjectToRoot(child);
  child->addComponent(std::make_shared<RigidBody>());

  collisionSystem.fixedUpdate(*scene.objectManager);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), parent, child));
}

TEST(CollisionEvent, ATriggerReportsTheContactWithoutMovingAnything)
{
  const auto scene = makeScene();
  const auto moving = addBody(scene, "Moving", { 0, 0, 0 }, true, true);
  const auto resting = addBody(scene, "Resting", { 1, 0, 0 }, false, true);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), moving, resting));

  // The whole point of a trigger: a volume a script can react to without it blocking anything.
  EXPECT_EQ(positionOf(moving), glm::vec3(0, 0, 0));
}

TEST(CollisionEvent, ASolidContactPushesTheBodyOut)
{
  const auto scene = makeScene();
  const auto moving = addBody(scene, "Moving", { 0, 0, 0 }, true);
  const auto resting = addBody(scene, "Resting", { 1, 0, 0 }, false);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager);

  // The contrast with the trigger above, and what makes that test mean something: the same overlap
  // between solid colliders both reports the contact and corrects the overlap.
  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), moving, resting));
  EXPECT_NE(positionOf(moving), glm::vec3(0, 0, 0));
}

TEST(CollisionEvent, ResetForgetsThePreviousTick)
{
  const auto scene = makeScene();
  const auto moving = addBody(scene, "Moving", { 0, 0, 0 }, true, true);
  const auto resting = addBody(scene, "Resting", { 1, 0, 0 }, false, true);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager);
  ASSERT_TRUE(contains(collisionSystem.getCollisionEnters(), moving, resting));

  collisionSystem.reset();

  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());
  EXPECT_TRUE(collisionSystem.getCollisionStays().empty());
  EXPECT_TRUE(collisionSystem.getCollisionExits().empty());

  // A scene stop and start runs the diff against an empty history, so the first tick of the new run
  // reports the contact as new rather than as a stay carried over from the previous one.
  collisionSystem.fixedUpdate(*scene.objectManager);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), moving, resting));
  EXPECT_TRUE(collisionSystem.getCollisionStays().empty());
}

TEST(CollisionEvent, AnObjectLeavingTheSceneExitsRatherThanLingering)
{
  const auto scene = makeScene();
  const auto moving = addBody(scene, "Moving", { 0, 0, 0 }, true, true);
  const auto resting = addBody(scene, "Resting", { 1, 0, 0 }, false, true);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager);
  ASSERT_TRUE(contains(collisionSystem.getCollisionEnters(), moving, resting));

  scene.objectManager->removeObject(resting);
  scene.objectManager->deleteObjectsMarkedForDeletion();

  // The pair has to leave through the exit list rather than being dropped silently, or a script that
  // paired an onCollisionEnter with an onCollisionExit never gets the second half.
  collisionSystem.fixedUpdate(*scene.objectManager);

  EXPECT_TRUE(contains(collisionSystem.getCollisionExits(), moving, resting));
}
