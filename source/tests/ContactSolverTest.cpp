#include <gtest/gtest.h>

#include "TestScene.h"
#include "CollisionSystem.h"
#include "ContactSolver.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "collisions/FaceManifold.h"
#include "collisions/NarrowPhase.h"
#include "objects/Object.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::positionOf;
  using fixtures::transformOf;
  using physicsFixtures::addBody;

  constexpr float serverTick = 1.0f / 50.0f;
  constexpr float gravityStep = 9.81f * PhysicsSystem::unitsPerMeter * serverTick * serverTick;

  // A scene on a static slab whose top face is y = 1, stepped the way the server steps it.
  struct World {
    fixtures::Scene scene = makeScene();
    CollisionSystem collisions;

    World()
    {
      const auto ground = addObject(scene, "Ground", { 0, 0, 0 }, { 20, 1, 20 });
      fixtures::addBoxCollider(ground);
    }

    // A dynamic box of the given half extents.
    std::shared_ptr<Object> addBox(const std::string& name, const glm::vec3& position,
                                   const glm::vec3& halfExtents = glm::vec3(1), const float mass = 10.0f)
    {
      const auto box = addObject(scene, name, position, halfExtents);
      fixtures::addBoxCollider(box);
      addBody(box, true)->setMass(mass);

      return box;
    }

    void run(const int ticks)
    {
      for (int tick = 0; tick < ticks; ++tick)
      {
        PhysicsSystem::fixedUpdate(*scene.objectManager, serverTick);
        collisions.fixedUpdate(*scene.objectManager, serverTick);
      }
    }
  };

  std::shared_ptr<RigidBody> bodyOf(const std::shared_ptr<Object>& object)
  {
    return object->getComponent<RigidBody>(ComponentType::rigidBody);
  }

  float tiltDegrees(const std::shared_ptr<Object>& object)
  {
    const auto up = transformOf(object)->getOrientation() * glm::vec3(0, 1, 0);

    return glm::degrees(std::acos(std::min(std::fabs(up.y), 1.0f)));
  }
}

TEST(ContactSolver, BodiesThatOnlyTouchAreHeldFromTheFirstTick)
{
  // Set down exactly on the slab and on each other, as a scene is authored. Nothing overlaps yet, and a contact
  // found only once it did would let each box drop a tick of gravity further than the one below it first.
  World world;
  const auto lower = world.addBox("Lower", { 0, 2, 0 });
  const auto upper = world.addBox("Upper", { 0, 4, 0 });

  world.run(1);

  // Each may close the small overlap a resting contact keeps, a twentieth of what a tick of gravity would move it.
  EXPECT_NEAR(positionOf(lower).y, 2.0f, 1e-3f);
  EXPECT_NEAR(positionOf(upper).y, 4.0f, 1e-3f);
  EXPECT_LT(glm::length(bodyOf(lower)->getVelocity()), 0.1f * gravityStep);
  EXPECT_LT(glm::length(bodyOf(upper)->getVelocity()), 0.1f * gravityStep);
  EXPECT_FALSE(bodyOf(upper)->isFalling());

  // Positive control: with nothing under it, the same box does fall that tick.
  World empty;
  const auto dropped = empty.addBox("Dropped", { 0, 8, 0 });
  empty.run(2);
  EXPECT_LT(positionOf(dropped).y, 8.0f - 0.5f * gravityStep);
}

TEST(ContactSolver, ATenBoxStackStandsStill)
{
  World world;
  std::vector<std::shared_ptr<Object>> boxes;
  for (int level = 0; level < 10; ++level)
  {
    boxes.push_back(world.addBox("Box", { 0, 2.0f + 2.0f * static_cast<float>(level), 0 }));
  }

  // Twenty seconds.
  world.run(1000);

  for (size_t level = 0; level < boxes.size(); ++level)
  {
    SCOPED_TRACE(level);
    const auto position = positionOf(boxes[level]);
    EXPECT_NEAR(position.y, 2.0f + 2.0f * static_cast<float>(level), 0.02f);
    EXPECT_LT(glm::length(glm::vec2(position.x, position.z)), 0.05f);
    EXPECT_LT(tiltDegrees(boxes[level]), 0.1f);
    EXPECT_LT(glm::length(bodyOf(boxes[level])->getVelocity()), 0.01f * gravityStep);
    EXPECT_LT(glm::length(bodyOf(boxes[level])->getAngularVelocity()), PhysicsSystem::restAngularSpeed);
  }
}

TEST(ContactSolver, AFastBodyStopsOnAThinFloorInsteadOfPassingThroughIt)
{
  // A sheet a twentieth of a unit thick, well above the slab, and bodies arriving at three units a tick: each
  // would be past the sheet before it ever overlapped it.
  for (const bool sphere : { false, true })
  {
    SCOPED_TRACE(sphere ? "sphere" : "box");

    World world;
    const auto sheet = addObject(world.scene, "Sheet", { 0, 10, 0 }, { 5, 0.025f, 5 });
    fixtures::addBoxCollider(sheet);

    const auto falling = addObject(world.scene, "Falling", { 0, 20, 0 }, glm::vec3(0.5f));
    if (sphere)
    {
      fixtures::addSphereCollider(falling, 1.0f);
    }
    else
    {
      fixtures::addBoxCollider(falling);
    }
    addBody(falling, true)->setVelocity({ 0, -3.0f, 0 });

    world.run(100);

    EXPECT_NEAR(positionOf(falling).y, 10.025f + 0.5f, 0.02f);
    EXPECT_LT(glm::length(bodyOf(falling)->getVelocity()), 0.01f * gravityStep);
  }
}

TEST(ContactSolver, BodiesCreatedInsideEachOtherComeApartAndRest)
{
  // What duplicating an object in the editor leaves: copies in the same place.
  World world;
  std::vector<std::shared_ptr<Object>> copies;
  for (int copy = 0; copy < 5; ++copy)
  {
    copies.push_back(world.addBox("Copy", { 0, 1.5f, 0 }, glm::vec3(0.5f), 1.0f));
  }

  world.run(500);

  for (size_t i = 0; i < copies.size(); ++i)
  {
    SCOPED_TRACE(i);
    const auto position = positionOf(copies[i]);
    ASSERT_TRUE(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z));
    EXPECT_GT(position.y, 1.4f);
    EXPECT_LT(glm::length(position), 20.0f);
    EXPECT_LT(glm::length(bodyOf(copies[i])->getVelocity()), 0.05f * gravityStep);
    EXPECT_LT(glm::length(bodyOf(copies[i])->getAngularVelocity()), 1.0f);

    for (size_t j = i + 1; j < copies.size(); ++j)
    {
      // Unit cubes cannot be closer than this without overlapping by more than a resting contact leaves.
      EXPECT_GT(glm::distance(position, positionOf(copies[j])), 0.95f) << "with " << j;
    }
  }
}

TEST(ContactSolver, FreeBodiesTradeTheirClosingSpeedAndALightOneReboundsOffAHeavyOne)
{
  const auto collide = [](const float firstMass, const float secondMass)
  {
    const auto scene = makeScene();
    const auto first = addObject(scene, "First", { -2, 0, 0 });
    fixtures::addSphereCollider(first, 1.0f);
    const auto firstBody = addBody(first, false);
    firstBody->setMass(firstMass);
    firstBody->setVelocity({ 0.2f, 0, 0 });

    const auto second = addObject(scene, "Second", { 2, 0, 0 });
    fixtures::addSphereCollider(second, 1.0f);
    const auto secondBody = addBody(second, false);
    secondBody->setMass(secondMass);

    CollisionSystem collisions;
    for (int tick = 0; tick < 30; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, serverTick);
      collisions.fixedUpdate(*scene.objectManager, serverTick);
    }

    return std::pair{ firstBody->getVelocity().x, secondBody->getVelocity().x };
  };

  // Equal masses swap: the first stops and the second leaves with its speed.
  const auto [stopped, launched] = collide(1.0f, 1.0f);
  EXPECT_NEAR(stopped, 0.0f, 1e-3f);
  EXPECT_NEAR(launched, 0.2f, 1e-3f);

  // A light body comes back off a heavy one, which barely moves.
  const auto [rebound, nudged] = collide(1.0f, 100.0f);
  EXPECT_LT(rebound, -0.19f);
  EXPECT_GT(nudged, 0.0f);
  EXPECT_LT(nudged, 0.01f);
}

TEST(ContactSolver, ARestingContactStopsTheBodyAndKeepsItsImpulseForTheNextTick)
{
  const auto scene = makeScene();
  const auto ground = addObject(scene, "Ground", { 0, 0, 0 }, { 5, 1, 5 });
  const auto groundCollider = fixtures::addBoxCollider(ground);
  const auto box = addObject(scene, "Box", { 0, 2, 0 });
  const auto collider = fixtures::addBoxCollider(box);
  const auto body = addBody(box, false);
  body->setVelocity({ 0, -0.1f, 0 });

  ContactPair pair{ body.get(), nullptr, collider.get(), groundCollider.get(), {} };
  pair.contact.minimumTranslationVector = { 0, 0.001f, 0 };
  pair.contact.points = { glm::vec3{ -1, 1, -1 }, glm::vec3{ 1, 1, -1 }, glm::vec3{ 1, 1, 1 }, glm::vec3{ -1, 1, 1 } };
  pair.contact.depths = { 0.001f, 0.001f, 0.001f, 0.001f };
  pair.contact.pointCount = 4;

  const ContactSolver::Cache empty;
  std::vector<ContactSolver::CachedContact> resolved;
  ContactSolver::solve(std::span<const ContactPair>(&pair, 1), empty, resolved, serverTick);

  EXPECT_NEAR(body->getVelocity().y, 0.0f, 1e-5f);
  EXPECT_NEAR(glm::length(body->getAngularVelocity()), 0.0f, 1e-3f);
  EXPECT_FALSE(body->getNextFalling());

  // The four corners together gave the impulse that stopped it: its mass times the speed it lost.
  ASSERT_EQ(resolved.size(), 1u);
  ASSERT_EQ(resolved[0].pointCount, 4);
  float total = 0.0f;
  for (const auto& point : resolved[0].points)
  {
    EXPECT_GE(point.normalImpulse, 0.0f);
    total += point.normalImpulse;
  }
  EXPECT_NEAR(total, body->getMass() * 0.1f, 1e-3f);

  // Started from those impulses, the same contact on a body moving the same way is answered before the first pass:
  // the solve ends where it began.
  body->setVelocity({ 0, -0.1f, 0 });
  body->setAngularVelocity(glm::vec3(0));
  ContactSolver::Cache carried;
  carried.emplace(ContactSolver::keyOf(collider.get(), groundCollider.get()), resolved[0]);
  std::vector<ContactSolver::CachedContact> again;
  ContactSolver::solve(std::span<const ContactPair>(&pair, 1), carried, again, serverTick);

  EXPECT_NEAR(body->getVelocity().y, 0.0f, 1e-5f);
  ASSERT_EQ(again.size(), 1u);
  for (size_t i = 0; i < 4; ++i)
  {
    EXPECT_NEAR(again[0].points[i].normalImpulse, resolved[0].points[i].normalImpulse, 1e-3f);
  }
}

TEST(ContactSolver, AHitOnARestingBodyBouncesLessTheMoreItPressesIntoTheSupport)
{
  // A ball resting on the slab is struck by another: level with the slab the two trade speed, and from straight
  // above the slab takes the blow and nothing bounces.
  World level;
  const auto resting = addObject(level.scene, "Resting", { 0, 2, 0 });
  fixtures::addSphereCollider(resting, 1.0f);
  addBody(resting, true)->setFriction(0.0f);
  level.run(5);

  const auto striker = addObject(level.scene, "Striker", { -4, 2, 0 });
  fixtures::addSphereCollider(striker, 1.0f);
  const auto strikerBody = addBody(striker, true);
  strikerBody->setFriction(0.0f);
  strikerBody->setVelocity({ 0.2f, 0, 0 });
  level.run(30);

  EXPECT_GT(bodyOf(resting)->getVelocity().x, 0.15f);
  EXPECT_LT(strikerBody->getVelocity().x, 0.05f);

  World above;
  const auto under = addObject(above.scene, "Resting", { 0, 2, 0 });
  fixtures::addSphereCollider(under, 1.0f);
  addBody(under, true);
  above.run(5);

  const auto dropped = addObject(above.scene, "Dropped", { 0, 6, 0 });
  fixtures::addSphereCollider(dropped, 1.0f);
  const auto droppedBody = addBody(dropped, true);
  droppedBody->setVelocity({ 0, -0.2f, 0 });

  float highestSpeed = 0.0f;
  for (int tick = 0; tick < 30; ++tick)
  {
    above.run(1);
    highestSpeed = std::max(highestSpeed, droppedBody->getVelocity().y);
  }
  EXPECT_LT(highestSpeed, 0.02f);
}

TEST(ContactSweep, APairAboutToMeetReportsTheNormalAndTheDistanceStillBetweenThem)
{
  const auto scene = makeScene();
  const auto left = addObject(scene, "Left", { 0, 0, 0 });
  const auto leftCollider = fixtures::addBoxCollider(left);
  const auto right = addObject(scene, "Right", { 2.3f, 0, 0 });
  const auto rightCollider = fixtures::addBoxCollider(right);

  // Three tenths apart: not overlapping, and not reached by a move of two tenths.
  EXPECT_FALSE(collisions::intersects(*leftCollider, *rightCollider));
  EXPECT_FALSE(collisions::approaches(*leftCollider, *rightCollider, { 0.2f, 0, 0 }));
  EXPECT_FALSE(collisions::findContact(*leftCollider, *rightCollider, glm::vec3(0.2f, 0, 0)).has_value());
  // Nor by any move away.
  EXPECT_FALSE(collisions::approaches(*leftCollider, *rightCollider, { -1.0f, 0, 0 }));

  EXPECT_TRUE(collisions::approaches(*leftCollider, *rightCollider, { 0.5f, 0, 0 }));
  const auto contact = collisions::findContact(*leftCollider, *rightCollider, glm::vec3(0.5f, 0, 0));
  ASSERT_TRUE(contact.has_value());

  // The normal pushes the first box back the way it came, and the overlap is the gap, negative.
  fixtures::expectNear("normal", contact->normal(), { -1, 0, 0 }, 1e-3f);
  EXPECT_NEAR(contact->advance, 0.5f, 1e-3f);
  EXPECT_NEAR(contact->overlap(), -0.3f, 1e-3f);
  ASSERT_GT(contact->contactPoints().size(), 1u);
  for (size_t i = 0; i < contact->contactPoints().size(); ++i)
  {
    EXPECT_NEAR(contact->depthAt(i), -0.3f, 1e-3f);
  }
}

TEST(ContactSweep, RoundShapesAboutToMeetReportTheirGapToo)
{
  const auto scene = makeScene();
  const auto left = addObject(scene, "Left", { 0, 0, 0 });
  const auto leftCollider = fixtures::addSphereCollider(left, 1.0f);
  const auto right = addObject(scene, "Right", { 2.3f, 0, 0 });
  const auto rightCollider = fixtures::addSphereCollider(right, 1.0f);

  EXPECT_FALSE(collisions::approaches(*leftCollider, *rightCollider, { 0.2f, 0, 0 }));
  EXPECT_TRUE(collisions::approaches(*leftCollider, *rightCollider, { 0.5f, 0, 0 }));

  const auto contact = collisions::findContact(*leftCollider, *rightCollider, glm::vec3(0.5f, 0, 0));
  ASSERT_TRUE(contact.has_value());
  fixtures::expectNear("normal", contact->normal(), { -1, 0, 0 }, 1e-4f);
  EXPECT_NEAR(contact->overlap(), -0.3f, 1e-4f);
  EXPECT_NEAR(contact->depthAt(0), -0.3f, 1e-4f);
}

TEST(ContactSweep, AFaceComingDownAtAnAngleReportsTheCornersThatHaveNotArrived)
{
  // A box two units wide tilted one degree, its low edge a hundredth into the slab: the high edge is 0.035 short
  // of the slab, inside the margin, so all four corners are reported, two of them with how far they have to go.
  const auto scene = makeScene();
  const auto ground = addObject(scene, "Ground", { 0, 0, 0 }, { 5, 1, 5 });
  const auto groundCollider = fixtures::addBoxCollider(ground);
  const auto box = addObject(scene, "Box", { 0, 2.0f + std::sin(glm::radians(1.0f)) - 0.01f, 0 });
  transformOf(box)->setRotation({ 0, 0, 1.0f });
  const auto collider = fixtures::addBoxCollider(box);

  const auto contact = collisions::findContact(*collider, *groundCollider);
  ASSERT_TRUE(contact.has_value());
  ASSERT_EQ(contact->contactPoints().size(), 4u);

  int touching = 0;
  int arriving = 0;
  for (size_t i = 0; i < 4; ++i)
  {
    const float depth = contact->depthAt(i);
    touching += depth > 0.0f ? 1 : 0;
    arriving += depth < -0.02f && depth > -collisions::contactMargin ? 1 : 0;
  }
  EXPECT_EQ(touching, 2);
  EXPECT_EQ(arriving, 2);

  // Positive control: tilted far enough to put the high edge out of the margin, only the low edge is reported.
  transformOf(box)->setRotation({ 0, 0, 10.0f });
  transformOf(box)->setPosition({ 0, 2.0f + std::sqrt(2.0f) * std::sin(glm::radians(55.0f)) - 1.0f - 0.01f, 0 });
  const auto steep = collisions::findContact(*collider, *groundCollider);
  ASSERT_TRUE(steep.has_value());
  EXPECT_EQ(steep->contactPoints().size(), 2u);
}

TEST(ContactSweep, AShapeMovingOutFromUnderAnotherIsNotStoppedAsIfItHadSweptThroughIt)
{
  // A box resting a thousandth into the top of a sphere, and the sphere moving down from under it. The way out of
  // that overlap is the way the sphere is going, which is also what a sweep through something thin looks like.
  const auto scene = makeScene();
  const auto sphere = addObject(scene, "Sphere", { 0, 0, 0 });
  const auto sphereCollider = fixtures::addSphereCollider(sphere, 1.0f);
  const auto box = addObject(scene, "Box", { 0, 1.999f, 0 });
  const auto boxCollider = fixtures::addBoxCollider(box);

  const auto leaving = collisions::findContact(*sphereCollider, *boxCollider, glm::vec3(0, -0.02f, 0));
  ASSERT_TRUE(leaving.has_value());
  fixtures::expectNear("normal", leaving->normal(), { 0, -1, 0 }, 1e-2f);
  EXPECT_NEAR(leaving->overlap(), 0.001f, 1e-3f);

  // Positive control: a sweep that does carry the sphere through a thin plate five hundredths below it is stopped
  // against the motion, with that gap still to close.
  const auto plate = addObject(scene, "Plate", { 0, -1.06f, 0 }, { 5, 0.01f, 5 });
  const auto plateCollider = fixtures::addBoxCollider(plate);

  const auto through = collisions::findContact(*sphereCollider, *plateCollider, glm::vec3(0, -5.0f, 0));
  ASSERT_TRUE(through.has_value());
  fixtures::expectNear("normal", through->normal(), { 0, 1, 0 }, 1e-2f);
  EXPECT_NEAR(through->overlap(), -0.05f, 1e-2f);
}
