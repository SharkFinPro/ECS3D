#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "collisions/NarrowPhase.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <glm/vec3.hpp>
#include <memory>
#include <utility>

namespace {
  // A collider needs a Transform on the same object to place its geometry; nothing else about the scene
  // matters to the narrow phase. The returned Object is the collider's lifetime anchor - it holds the
  // Transform the collider keeps a weak pointer to, and the raw owner pointer behind it.
  template <typename T>
  std::pair<std::shared_ptr<Object>, std::shared_ptr<T>> makeCollider(const glm::vec3& position,
                                                                      const glm::vec3& scale = glm::vec3(1))
  {
    auto object = std::make_shared<Object>("Collider");
    const auto transform = object->getComponent<Transform>(ComponentType::transform);
    transform->setPosition(position);
    transform->setScale(scale);

    auto collider = std::make_shared<T>();
    object->addComponent(collider);

    return { object, collider };
  }

  void moveTo(const std::shared_ptr<Object>& object, const glm::vec3& position)
  {
    object->getComponent<Transform>(ComponentType::transform)->setPosition(position);
  }

  // EPA converges rather than terminating exactly, and every coordinate has been through a world
  // matrix, so nothing here is compared exactly.
  constexpr float tolerance = 1e-3f;

  void expectNear(const char* what, const glm::vec3& actual, const glm::vec3& expected)
  {
    SCOPED_TRACE(::testing::Message()
                 << what
                 << ": expected (" << expected.x << ", " << expected.y << ", " << expected.z << ")"
                 << ", got (" << actual.x << ", " << actual.y << ", " << actual.z << ")");

    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }
}

// A box collider is the unit box [-1,1]^3 through its world matrix, so at scale 1 a box centered on the
// origin spans -1 to 1 on every axis. Every depth below is built on that.

TEST(NarrowPhase, ReportsNoContactForBoxesThatDoNotOverlap)
{
  const auto [firstObject, first] = makeCollider<BoxCollider>({ 0, 0, 0 });
  const auto [secondObject, second] = makeCollider<BoxCollider>({ 5, 0, 0 });

  EXPECT_FALSE(findContact(first.get(), second).has_value());
  EXPECT_FALSE(intersects(first.get(), second));

  // The control: the same pair, moved into contact. Without it this test would pass just as well
  // against a narrow phase that had stopped detecting anything at all.
  moveTo(secondObject, { 1.5f, 0, 0 });

  EXPECT_TRUE(findContact(first.get(), second).has_value());
  EXPECT_TRUE(intersects(first.get(), second));
}

TEST(NarrowPhase, MeasuresBoxPenetrationDepthAlongTheShallowestAxis)
{
  const auto [firstObject, first] = makeCollider<BoxCollider>({ 0, 0, 0 });

  // Spans 0.5 to 2.5 on x and -1 to 1 on the other two, so it overlaps the first box by 0.5 on x and by
  // the full 2 on y and z. EPA has to find the x face rather than one of the deeper ones.
  const auto [secondObject, second] = makeCollider<BoxCollider>({ 1.5f, 0, 0 });

  const auto contact = findContact(first.get(), second);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.5f, tolerance);
  expectNear("normal", contact->normal(), { -1, 0, 0 });
}

TEST(NarrowPhase, PointsTheNormalAwayFromTheOtherColliderWhicheverSideItIsOn)
{
  const auto [firstObject, first] = makeCollider<BoxCollider>({ 0, 0, 0 });
  const auto [secondObject, second] = makeCollider<BoxCollider>({ -1.5f, 0, 0 });

  // Mirrored, so the sign convention is pinned rather than confirmed once on a side where it could have
  // been either way round.
  const auto contact = findContact(first.get(), second);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.5f, tolerance);
  expectNear("normal", contact->normal(), { 1, 0, 0 });
}

TEST(NarrowPhase, TakesTheOverlapFromTheScaledBoxNotTheUnitOne)
{
  const auto [firstObject, first] = makeCollider<BoxCollider>({ 0, 0, 0 }, { 3, 1, 1 });

  // The first box now spans -3 to 3 on x, so a unit box spanning 1.5 to 3.5 overlaps it by 1.5 - which
  // is only true if the scale reached the support function. Unscaled the pair would not touch at all.
  const auto [secondObject, second] = makeCollider<BoxCollider>({ 2.5f, 0, 0 });

  const auto contact = findContact(first.get(), second);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 1.5f, tolerance);
  expectNear("normal", contact->normal(), { -1, 0, 0 });
}

TEST(NarrowPhase, MeasuresSpherePenetrationFromTheCentersAndRadii)
{
  const auto [firstObject, first] = makeCollider<SphereCollider>({ 0, 0, 0 });
  const auto [secondObject, second] = makeCollider<SphereCollider>({ 1.5f, 0, 0 });

  // Two unit spheres 1.5 apart overlap by 0.5. This pair skips GJK entirely - the sphere-sphere path is
  // closed form - so it is the one case whose depth is arrived at rather than converged on.
  const auto contact = findContact(first.get(), second);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.5f, tolerance);
  expectNear("normal", contact->normal(), { -1, 0, 0 });

  // On the surface of the first sphere, on the line between the centers.
  expectNear("point", contact->point, { 1, 0, 0 });
}

TEST(NarrowPhase, PushesConcentricSpheresApartAlongY)
{
  const auto [firstObject, first] = makeCollider<SphereCollider>({ 0, 0, 0 });
  const auto [secondObject, second] = makeCollider<SphereCollider>({ 0, 0, 0 });

  // There is no separating direction to normalize here, so the fallback is an arbitrary axis. What
  // matters is that it is not zero: a zero translation would leave the pair welded together forever.
  const auto contact = findContact(first.get(), second);
  ASSERT_TRUE(contact.has_value());

  EXPECT_GT(contact->depth(), 0.0f);
  expectNear("normal", contact->normal(), { 0, 1, 0 });
}

TEST(NarrowPhase, TreatsSpheresThatOnlyTouchAsApart)
{
  const auto [firstObject, first] = makeCollider<SphereCollider>({ 0, 0, 0 });
  const auto [secondObject, second] = makeCollider<SphereCollider>({ 2, 0, 0 });

  // Exactly the combined radius apart: touching is not overlapping.
  EXPECT_FALSE(findContact(first.get(), second).has_value());

  moveTo(secondObject, { 1.99f, 0, 0 });

  EXPECT_TRUE(findContact(first.get(), second).has_value());
}

TEST(NarrowPhase, MeasuresABoxAgainstASphere)
{
  const auto [firstObject, first] = makeCollider<BoxCollider>({ 0, 0, 0 });

  // A unit sphere centered at 1.5 on x reaches back to 0.5, which is 0.5 inside the box's +x face. A
  // mixed pair goes through GJK/EPA rather than the closed-form sphere path.
  const auto [secondObject, second] = makeCollider<SphereCollider>({ 1.5f, 0, 0 });

  // A box's support points sit on its vertices, so EPA lands on the answer; a sphere's lie anywhere on
  // a curve, so it approaches one. Hence the looser bound on this pair alone.
  constexpr float curvedTolerance = 1e-2f;

  const auto contact = findContact(first.get(), second);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.5f, curvedTolerance);
  EXPECT_NEAR(contact->normal().x, -1.0f, curvedTolerance);
  EXPECT_NEAR(contact->normal().y, 0.0f, curvedTolerance);
  EXPECT_NEAR(contact->normal().z, 0.0f, curvedTolerance);
}

TEST(NarrowPhase, DepthGrowsWithTheOverlap)
{
  const auto [firstObject, first] = makeCollider<BoxCollider>({ 0, 0, 0 });
  const auto [secondObject, second] = makeCollider<BoxCollider>({ 1.9f, 0, 0 });

  const auto shallow = findContact(first.get(), second);
  ASSERT_TRUE(shallow.has_value());

  moveTo(secondObject, { 1.0f, 0, 0 });

  const auto deep = findContact(first.get(), second);
  ASSERT_TRUE(deep.has_value());

  // The relation as well as the numbers: a depth that is constant, or that runs backwards, is a defect
  // no single-position assertion above would catch.
  EXPECT_GT(deep->depth(), shallow->depth());
  EXPECT_NEAR(shallow->depth(), 0.1f, tolerance);
  EXPECT_NEAR(deep->depth(), 1.0f, tolerance);
}

TEST(NarrowPhase, EveryContactIsAlsoAnIntersection)
{
  const auto [firstObject, first] = makeCollider<BoxCollider>({ 0, 0, 0 });
  const auto [secondObject, second] = makeCollider<BoxCollider>({ 0, 0, 0 });

  // The cheap predicate is what the sweep runs on every pair; the full one is what the response runs on
  // the pairs it keeps. They reach GJK by separate paths, so a contact the sweep would not have recorded
  // is a pair the response resolves without an event ever being fired for it.
  //
  // Only this direction is an invariant. The other way round is not: intersects answers yes for a pair
  // whose translation out of the overlap could not be built, which findContact reports as no contact.
  for (int step = 0; step <= 40; ++step)
  {
    const float x = static_cast<float>(step) * 0.1f;
    moveTo(secondObject, { x, 0, 0 });

    SCOPED_TRACE(::testing::Message() << "x = " << x);

    if (findContact(first.get(), second).has_value())
    {
      EXPECT_TRUE(intersects(first.get(), second));
    }
  }

  // The controls, so the loop above cannot be satisfied by a narrow phase that finds nothing anywhere.
  moveTo(secondObject, { 1.0f, 0, 0 });
  EXPECT_TRUE(findContact(first.get(), second).has_value());
  EXPECT_TRUE(intersects(first.get(), second));

  moveTo(secondObject, { 5.0f, 0, 0 });
  EXPECT_FALSE(findContact(first.get(), second).has_value());
  EXPECT_FALSE(intersects(first.get(), second));
}
