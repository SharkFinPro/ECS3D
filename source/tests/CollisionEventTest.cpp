#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "TestScene.h"
#include "CollisionSystem.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/Collider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>
#include <uuid.h>

namespace {
  using fixtures::expectNear;
  using fixtures::makeScene;
  using fixtures::positionOf;
  using fixtures::Scene;

  // The server's tick, which the responses to the contacts below are worked out at.
  constexpr float dt = 1.0f / 50.0f;

  // Whether the object gets a RigidBody decides whether it is a collision *source*: the sweep skips an
  // edge with no rigid body entirely, so a pair of static colliders is never even tested.
  std::shared_ptr<Object> addBody(const Scene& scene, const std::string& name, const glm::vec3& position,
                                  const bool dynamic, const bool trigger = false)
  {
    auto object = addObject(scene, name, position);
    fixtures::addBoxCollider(object)->setIsTrigger(trigger);

    if (dynamic)
    {
      fixtures::addRigidBody(object);
    }

    return object;
  }

  bool contains(const std::vector<CollisionPair>& pairs, const std::shared_ptr<Object>& a,
                const std::shared_ptr<Object>& b)
  {
    return std::ranges::find(pairs, CollisionPair::make(a->getUUID(), b->getUUID())) != pairs.end();
  }

  struct SingleContactResult {
    bool entered;
    glm::vec3 moverPosition;
  };

  // A dynamic body one unit into a static one: the single-candidate path of handleCollisions.
  SingleContactResult runSingleContact(const bool moverIsTrigger, const bool restingIsTrigger)
  {
    const auto scene = makeScene();
    const auto mover = addBody(scene, "Mover", { 0, 0, 0 }, true, moverIsTrigger);
    const auto resting = addBody(scene, "Resting", { 1, 0, 0 }, false, restingIsTrigger);

    CollisionSystem collisionSystem;
    collisionSystem.fixedUpdate(*scene.objectManager, dt);

    return { contains(collisionSystem.getCollisionEnters(), mover, resting), positionOf(mover) };
  }

  struct ApproachingBodiesResult {
    bool entered;
    glm::vec3 firstPosition;
    glm::vec3 secondPosition;
    glm::vec3 firstVelocity;
    glm::vec3 secondVelocity;
  };

  constexpr glm::vec3 firstApproach{ 0.1f, 0, 0 };
  constexpr glm::vec3 secondApproach{ -0.1f, 0, 0 };

  // Two dynamic bodies overlapping by one unit and moving toward each other: both edges are collision
  // sources, and a response would change positions and velocities on both sides.
  ApproachingBodiesResult runApproachingBodies(const bool firstIsTrigger, const bool secondIsTrigger)
  {
    const auto scene = makeScene();
    const auto first = addBody(scene, "First", { 0, 0, 0 }, true, firstIsTrigger);
    const auto second = addBody(scene, "Second", { 1, 0, 0 }, true, secondIsTrigger);
    first->getComponent<RigidBody>(ComponentType::rigidBody)->setVelocity(firstApproach);
    second->getComponent<RigidBody>(ComponentType::rigidBody)->setVelocity(secondApproach);

    CollisionSystem collisionSystem;
    collisionSystem.fixedUpdate(*scene.objectManager, dt);

    return { contains(collisionSystem.getCollisionEnters(), first, second), positionOf(first), positionOf(second),
             first->getComponent<RigidBody>(ComponentType::rigidBody)->getVelocity(),
             second->getComponent<RigidBody>(ComponentType::rigidBody)->getVelocity() };
  }

  struct MultiContactResult {
    bool enteredShallow;
    bool enteredDeep;
    glm::vec3 moverPosition;
  };

  // The geometry of TwoGenuineContactsInOneTickBothPushTheBodyClear: Shallow overlaps by 0.5 on x and Deep
  // by 1.0 on y, so the multi-candidate path resolves Deep first.
  MultiContactResult runTwoContacts(const bool moverIsTrigger, const bool shallowIsTrigger,
                                    const bool deepIsTrigger)
  {
    const auto scene = makeScene();
    const auto mover = addBody(scene, "Mover", { 0, 0, 0 }, true, moverIsTrigger);
    const auto shallow = addBody(scene, "Shallow", { 1.5f, 0, 0 }, false, shallowIsTrigger);
    const auto deep = addBody(scene, "Deep", { 0, 1.0f, 0 }, false, deepIsTrigger);

    CollisionSystem collisionSystem;
    collisionSystem.fixedUpdate(*scene.objectManager, dt);

    return { contains(collisionSystem.getCollisionEnters(), mover, shallow),
             contains(collisionSystem.getCollisionEnters(), mover, deep), positionOf(mover) };
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
  const auto moving = addBody(scene, "Moving", { 0, 0, 0 }, true, true);
  const auto resting = addBody(scene, "Resting", { 1, 0, 0 }, false, true);

  CollisionSystem collisionSystem;

  collisionSystem.fixedUpdate(*scene.objectManager, dt);
  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), moving, resting));
  EXPECT_TRUE(collisionSystem.getCollisionStays().empty());
  EXPECT_TRUE(collisionSystem.getCollisionExits().empty());

  // Nothing moved, so the same contact is a stay rather than a second enter.
  collisionSystem.fixedUpdate(*scene.objectManager, dt);
  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());
  EXPECT_TRUE(contains(collisionSystem.getCollisionStays(), moving, resting));
  EXPECT_TRUE(collisionSystem.getCollisionExits().empty());

  moving->getComponent<Transform>(ComponentType::transform)->setPosition({ 50, 0, 0 });

  collisionSystem.fixedUpdate(*scene.objectManager, dt);
  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());
  EXPECT_TRUE(collisionSystem.getCollisionStays().empty());
  EXPECT_TRUE(contains(collisionSystem.getCollisionExits(), moving, resting));

  // And the exit is reported once, not every tick after it.
  collisionSystem.fixedUpdate(*scene.objectManager, dt);
  EXPECT_TRUE(collisionSystem.getCollisionExits().empty());
}

TEST(CollisionEvent, ObjectsThatNeverTouchProduceNothing)
{
  const auto scene = makeScene();
  const auto a = addBody(scene, "A", { 0, 0, 0 }, true, true);
  const auto b = addBody(scene, "B", { 50, 0, 0 }, true, true);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());
  EXPECT_TRUE(collisionSystem.getCollisionStays().empty());
  EXPECT_TRUE(collisionSystem.getCollisionExits().empty());

  // Moved into contact, so the silence above is the distance and not a narrow phase that answers no to
  // everything. Every absence assertion in this file carries one of these.
  b->getComponent<Transform>(ComponentType::transform)->setPosition({ 1, 0, 0 });
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), a, b));
}

TEST(CollisionEvent, APairSeenFromBothSidesIsRecordedOnce)
{
  const auto scene = makeScene();
  const auto a = addBody(scene, "A", { 0, 0, 0 }, true, true);
  const auto b = addBody(scene, "B", { 1, 0, 0 }, true, true);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

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
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  // Overlapping, but neither is a collision source: the sweep skips an edge with no rigid body, so two
  // pieces of static geometry sharing a space cost nothing and report nothing.
  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());

  // The same overlap, with one of them given a body. This is what makes the silence above about the
  // missing body rather than about the boxes not touching.
  a->addComponent(std::make_shared<RigidBody>());
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

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
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  // The filter sits ahead of the narrow phase, so a filtered pair produces neither a response nor an
  // event - not an event without a response, which is what a trigger is.
  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());

  // Each mask now admits the other's layer, and the same overlap becomes a contact.
  a->getComponent<Collider>(ComponentType::collider)->setMask(1u << 2);
  b->getComponent<Collider>(ComponentType::collider)->setMask(1u << 1);

  collisionSystem.fixedUpdate(*scene.objectManager, dt);
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
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());

  // B admitting A as well turns the same overlap into a contact, so the refusal above is the one-sided
  // mask and not the geometry.
  b->getComponent<Collider>(ComponentType::collider)->setMask(1u << 1);
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

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
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  // A child's transform is relative to its parent, so the two occupy the same space by construction.
  // Reporting that as a contact would fire an event on every tick of every composed object.
  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());

  // Detached, and the very same overlap is a contact. Without this the test passes just as well against
  // a narrow phase that answers no to everything.
  child->setParent(nullptr);
  parent->removeChild(child);
  scene.objectManager->addObjectToRoot(child);
  child->addComponent(std::make_shared<RigidBody>());

  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), parent, child));
}

TEST(CollisionEvent, ATriggerReportsTheContactWithoutMovingAnything)
{
  const auto scene = makeScene();
  const auto moving = addBody(scene, "Moving", { 0, 0, 0 }, true, true);
  const auto resting = addBody(scene, "Resting", { 1, 0, 0 }, false, true);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

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
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  // The contrast with the trigger above, and what makes that test mean something: the same overlap
  // between solid colliders both reports the contact and corrects the overlap.
  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), moving, resting));
  EXPECT_NE(positionOf(moving), glm::vec3(0, 0, 0));
}

TEST(CollisionEvent, SeveralIndependentDynamicPairsAllResolveInOneTick)
{
  // Three dynamic-vs-static pairs, far enough apart that no pair's colliders are ever candidates for
  // another pair's sweep. Each edge that finds a collision has its own rigid body, so all three run as
  // separate parallel-loop iterations and each applies its own response - the shape checkCollisions'
  // narrow phase takes whenever more than one body is moving in the same tick. The narrow phase used to
  // apply that response from inside the parallel loop, which is safe here only because the pairs never
  // share a collider; the fix moves it to a serial pass after the loop, and this test pins that the
  // three independent contacts still resolve to the same result that shape had before.
  const auto scene = makeScene();

  const auto moverA = addBody(scene, "MoverA", { 0, 0, 0 }, true);
  addBody(scene, "RestingA", { 1, 0, 0 }, false);

  const auto moverB = addBody(scene, "MoverB", { 100, 0, 0 }, true);
  addBody(scene, "RestingB", { 101, 0, 0 }, false);

  const auto moverC = addBody(scene, "MoverC", { 200, 0, 0 }, true);
  addBody(scene, "RestingC", { 201, 0, 0 }, false);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  // Same geometry as ASolidContactPushesTheBodyOut's single pair (unit boxes one apart, a full unit of
  // overlap), repeated three times, so each mover is pushed exactly 1 clear on x and nowhere else.
  expectNear(positionOf(moverA), { -1.0f, 0, 0 }, 1e-3f);
  expectNear(positionOf(moverB), { 99.0f, 0, 0 }, 1e-3f);
  expectNear(positionOf(moverC), { 199.0f, 0, 0 }, 1e-3f);
}

TEST(CollisionEvent, TwoGenuineContactsInOneTickBothPushTheBodyClear)
{
  const auto scene = makeScene();

  // Overlaps both by a different amount on a different axis, so a response that resolved only one of
  // them - or resolved one twice - would leave a gap in the final position rather than a subtler numeric
  // drift. The other two axes line up exactly with each candidate, which is what makes the overlap on
  // its own axis the shallowest one the narrow phase can find.
  const auto moving = addBody(scene, "Moving", { 0, 0, 0 }, true);
  const auto shallow = addBody(scene, "Shallow", { 1.5f, 0, 0 }, false);
  const auto deep = addBody(scene, "Deep", { 0, 1.0f, 0 }, false);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  ASSERT_TRUE(contains(collisionSystem.getCollisionEnters(), moving, shallow));
  ASSERT_TRUE(contains(collisionSystem.getCollisionEnters(), moving, deep));

  // Both contacts are real, so the multi-contact path has to resolve both in the same tick: 0.5 clear of
  // Shallow on x, 1.0 clear of Deep on y.
  expectNear(positionOf(moving), { -0.5f, -1.0f, 0 }, 1e-3f);
}

TEST(CollisionEvent, EachContactAloneMatchesItsShareOfTheCombinedResponse)
{
  // The positive control for the test above: resolving Shallow or Deep on its own has to move Moving by
  // exactly the piece the combined tick attributed to it, or the combined result could be two contacts
  // agreeing on a coincidentally plausible sum rather than each genuinely being resolved.
  {
    const auto scene = makeScene();
    const auto moving = addBody(scene, "Moving", { 0, 0, 0 }, true);
    addBody(scene, "Shallow", { 1.5f, 0, 0 }, false);

    CollisionSystem collisionSystem;
    collisionSystem.fixedUpdate(*scene.objectManager, dt);

    expectNear(positionOf(moving), { -0.5f, 0, 0 }, 1e-3f);
  }

  {
    const auto scene = makeScene();
    const auto moving = addBody(scene, "Moving", { 0, 0, 0 }, true);
    addBody(scene, "Deep", { 0, 1.0f, 0 }, false);

    CollisionSystem collisionSystem;
    collisionSystem.fixedUpdate(*scene.objectManager, dt);

    expectNear(positionOf(moving), { 0, -1.0f, 0 }, 1e-3f);
  }
}

TEST(CollisionEvent, AContactTheFirstResponseAlreadyClearedIsNotAppliedAgain)
{
  const auto scene = makeScene();

  // Deep sits 0.5 into Moving from below. Shallow overlaps a corner of Moving, 0.1 on x and 0.2 on y,
  // so on its own it would push Moving 0.1 along -x. Resolving Deep first lifts Moving by 0.5, clear of
  // Shallow entirely, so Shallow's 0.1 measured before that lift must not be applied afterwards.
  const auto moving = addBody(scene, "Moving", { 0, 0, 0 }, true);
  const auto deep = addBody(scene, "Deep", { 0, -1.5f, 0 }, false);
  const auto shallow = addBody(scene, "Shallow", { 1.9f, -1.8f, 0 }, false);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  ASSERT_TRUE(contains(collisionSystem.getCollisionEnters(), moving, deep));
  ASSERT_TRUE(contains(collisionSystem.getCollisionEnters(), moving, shallow));

  expectNear(positionOf(moving), { 0, 0.5f, 0 }, 1e-3f);
}

TEST(CollisionEvent, AOneSidedTriggerReportsTheContactButAppliesNoResponse)
{
  // The positive control: neither side a trigger, and the same overlap is corrected.
  const auto solid = runSingleContact(false, false);
  ASSERT_TRUE(solid.entered);
  expectNear(solid.moverPosition, { -1.0f, 0, 0 }, 1e-3f);

  // Either side being a trigger is enough, whichever one is the moving body.
  const auto triggerMover = runSingleContact(true, false);
  EXPECT_TRUE(triggerMover.entered);
  expectNear(triggerMover.moverPosition, { 0, 0, 0 }, 1e-6f);

  const auto triggerResting = runSingleContact(false, true);
  EXPECT_TRUE(triggerResting.entered);
  expectNear(triggerResting.moverPosition, { 0, 0, 0 }, 1e-6f);
}

TEST(CollisionEvent, OneSidedTriggersAmongSeveralContactsSkipOnlyTheirOwnResponse)
{
  // The positive control: all solid, and both contacts are resolved (see the combined test above).
  const auto solid = runTwoContacts(false, false, false);
  ASSERT_TRUE(solid.enteredShallow);
  ASSERT_TRUE(solid.enteredDeep);
  expectNear(solid.moverPosition, { -0.5f, -1.0f, 0 }, 1e-3f);

  // The deeper contact is a trigger: it still reports, and only the shallow one pushes.
  const auto deepTrigger = runTwoContacts(false, false, true);
  EXPECT_TRUE(deepTrigger.enteredShallow);
  EXPECT_TRUE(deepTrigger.enteredDeep);
  expectNear(deepTrigger.moverPosition, { -0.5f, 0, 0 }, 1e-3f);

  // The shallower contact is a trigger: it still reports, and only the deep one pushes.
  const auto shallowTrigger = runTwoContacts(false, true, false);
  EXPECT_TRUE(shallowTrigger.enteredShallow);
  EXPECT_TRUE(shallowTrigger.enteredDeep);
  expectNear(shallowTrigger.moverPosition, { 0, -1.0f, 0 }, 1e-3f);

  // The moving body itself is the trigger: both contacts report and neither pushes.
  const auto triggerMover = runTwoContacts(true, false, false);
  EXPECT_TRUE(triggerMover.enteredShallow);
  EXPECT_TRUE(triggerMover.enteredDeep);
  expectNear(triggerMover.moverPosition, { 0, 0, 0 }, 1e-6f);
}

TEST(CollisionEvent, AOneSidedTriggerBetweenTwoMovingBodiesChangesNeitherOfThem)
{
  // The positive control: neither a trigger, and both bodies are pushed apart and have their velocity
  // changed.
  const auto solid = runApproachingBodies(false, false);
  ASSERT_TRUE(solid.entered);
  EXPECT_LT(solid.firstPosition.x, -1e-3f);
  EXPECT_GT(solid.secondPosition.x, 1.0f + 1e-3f);
  EXPECT_NE(solid.firstVelocity, firstApproach);
  EXPECT_NE(solid.secondVelocity, secondApproach);

  // One trigger side, whichever it is, leaves both bodies exactly as they were: no push on the partner and
  // no impulse on either.
  for (const auto& [firstIsTrigger, secondIsTrigger] : { std::pair{ true, false }, std::pair{ false, true } })
  {
    SCOPED_TRACE(::testing::Message() << "first trigger: " << firstIsTrigger);

    const auto result = runApproachingBodies(firstIsTrigger, secondIsTrigger);
    EXPECT_TRUE(result.entered);
    expectNear(result.firstPosition, { 0, 0, 0 }, 1e-6f);
    expectNear(result.secondPosition, { 1, 0, 0 }, 1e-6f);
    expectNear(result.firstVelocity, firstApproach, 1e-6f);
    expectNear(result.secondVelocity, secondApproach, 1e-6f);
  }
}

TEST(CollisionEvent, ResetForgetsThePreviousTick)
{
  const auto scene = makeScene();
  const auto moving = addBody(scene, "Moving", { 0, 0, 0 }, true, true);
  const auto resting = addBody(scene, "Resting", { 1, 0, 0 }, false, true);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);
  ASSERT_TRUE(contains(collisionSystem.getCollisionEnters(), moving, resting));

  collisionSystem.reset();

  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());
  EXPECT_TRUE(collisionSystem.getCollisionStays().empty());
  EXPECT_TRUE(collisionSystem.getCollisionExits().empty());

  // A scene stop and start runs the diff against an empty history, so the first tick of the new run
  // reports the contact as new rather than as a stay carried over from the previous one.
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), moving, resting));
  EXPECT_TRUE(collisionSystem.getCollisionStays().empty());
}

TEST(CollisionEvent, AnObjectLeavingTheSceneExitsRatherThanLingering)
{
  const auto scene = makeScene();
  const auto moving = addBody(scene, "Moving", { 0, 0, 0 }, true, true);
  const auto resting = addBody(scene, "Resting", { 1, 0, 0 }, false, true);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);
  ASSERT_TRUE(contains(collisionSystem.getCollisionEnters(), moving, resting));

  scene.objectManager->removeObject(resting);
  scene.objectManager->deleteObjectsMarkedForDeletion();

  // The pair has to leave through the exit list rather than being dropped silently, or a script that
  // paired an onCollisionEnter with an onCollisionExit never gets the second half.
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(contains(collisionSystem.getCollisionExits(), moving, resting));
}

TEST(CollisionEvent, ASphereMissingItsTransformDoesNotAbandonTheTick)
{
  const auto scene = makeScene();

  // Overlapping spheres, one of which has lost the Transform its collider resolves position and radius
  // through.
  const auto strippedSphere = addObject(scene, "StrippedSphere", { 0, 0, 0 });
  fixtures::addSphereCollider(strippedSphere, 1.0f);
  fixtures::addRigidBody(strippedSphere);
  strippedSphere->removeComponent(strippedSphere->getComponent<Transform>(ComponentType::transform));

  const auto otherSphere = addObject(scene, "OtherSphere", { 0.5f, 0, 0 });
  fixtures::addSphereCollider(otherSphere, 1.0f);
  fixtures::addRigidBody(otherSphere);

  // Positive control, well clear of the sphere pair above: a genuinely overlapping box pair that proves
  // the tick's other collision work still ran.
  const auto boxA = addBody(scene, "BoxA", { 50, 0, 0 }, true);
  const auto boxB = addBody(scene, "BoxB", { 51, 0, 0 }, true);

  CollisionSystem collisionSystem;
  EXPECT_NO_THROW(collisionSystem.fixedUpdate(*scene.objectManager, dt));

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), boxA, boxB));
}

TEST(CollisionEvent, ABoxMissingItsTransformDoesNotAbandonTheTick)
{
  const auto scene = makeScene();

  // Same case as the sphere test above, for the box collider.
  const auto strippedBox = addObject(scene, "StrippedBox", { 0, 0, 0 });
  fixtures::addBoxCollider(strippedBox);
  fixtures::addRigidBody(strippedBox);
  strippedBox->removeComponent(strippedBox->getComponent<Transform>(ComponentType::transform));

  const auto otherBox = addObject(scene, "OtherBox", { 0.5f, 0, 0 });
  fixtures::addBoxCollider(otherBox);
  fixtures::addRigidBody(otherBox);

  // Positive control, well clear of the box pair above: a genuinely overlapping sphere pair that proves
  // the tick's other collision work still ran.
  const auto sphereA = addObject(scene, "SphereA", { 50, 0, 0 });
  fixtures::addSphereCollider(sphereA, 1.0f);
  fixtures::addRigidBody(sphereA);

  const auto sphereB = addObject(scene, "SphereB", { 50.5f, 0, 0 });
  fixtures::addSphereCollider(sphereB, 1.0f);
  fixtures::addRigidBody(sphereB);

  CollisionSystem collisionSystem;
  EXPECT_NO_THROW(collisionSystem.fixedUpdate(*scene.objectManager, dt));

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), sphereA, sphereB));
}
