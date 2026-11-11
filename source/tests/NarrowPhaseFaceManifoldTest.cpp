#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "TestScene.h"
#include "collisions/NarrowPhase.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/Collider.h"
#include "objects/components/collisions/ConvexMeshCollider.h"

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <random>
#include <span>
#include <vector>

namespace {
  using fixtures::addObject;
  using fixtures::transformOf;

  constexpr float tolerance = 1e-3f;

  // A domino 0.3 by 2 by 1 and a pad 40 by 0.05 by 10: the small box's base lies wholly inside the pad's top.
  const glm::vec3 dominoScale{ 0.15f, 1.0f, 0.5f };
  const glm::vec3 padScale{ 20.0f, 0.025f, 5.0f };
  constexpr float overlap = 0.005f;

  enum class Shape {
    box,
    hull
  };

  std::shared_ptr<Collider> makeCollider(const std::shared_ptr<Object>& object, const Shape shape)
  {
    if (shape == Shape::box)
    {
      return fixtures::addBoxCollider(object);
    }

    const auto collider = std::make_shared<ConvexMeshCollider>();
    object->addComponent(collider);

    return collider;
  }

  struct Resting {
    fixtures::Scene scene;
    std::shared_ptr<Object> domino;
    std::shared_ptr<Collider> dominoCollider;
    std::shared_ptr<Collider> padCollider;

    Resting(const Shape dominoShape, const Shape padShape, const float yawDegrees)
    {
      const auto pad = addObject(scene, "Pad", { 0, 0, 0 }, padScale);
      padCollider = makeCollider(pad, padShape);

      domino = addObject(scene, "Domino", { 0.3f, padScale.y + dominoScale.y - overlap, -0.2f }, dominoScale);
      transformOf(domino)->setRotation({ 0, yawDegrees, 0 });
      dominoCollider = makeCollider(domino, dominoShape);
    }

    // Where the domino's four base corners are, from its own transform.
    [[nodiscard]] std::array<glm::vec3, 4> baseCorners() const
    {
      const auto& transform = *transformOf(domino);
      std::array<glm::vec3, 4> corners;
      int next = 0;
      for (const float x : { -dominoScale.x, dominoScale.x })
      {
        for (const float z : { -dominoScale.z, dominoScale.z })
        {
          corners[next++] = transform.getPosition() + transform.getOrientation() * glm::vec3(x, -dominoScale.y, z);
        }
      }

      return corners;
    }
  };

  // Every corner is covered by a contact point, and every point is a corner: the points are the base and nothing
  // else. Heights are compared loosely, since the points sit between the two faces.
  void expectPointsAreTheBase(const collisions::Contact& contact, const Resting& resting)
  {
    const auto corners = resting.baseCorners();
    const auto points = contact.contactPoints();

    ASSERT_EQ(points.size(), 4u);

    const auto planDistance = [](const glm::vec3& a, const glm::vec3& b)
    {
      return glm::distance(glm::vec3(a.x, 0.0f, a.z), glm::vec3(b.x, 0.0f, b.z));
    };

    for (const auto& corner : corners)
    {
      bool covered = false;
      for (const auto& point : points)
      {
        covered = covered || planDistance(point, corner) <= tolerance;
      }
      EXPECT_TRUE(covered) << "no point under corner (" << corner.x << ", " << corner.z << ")";
    }

    for (const auto& point : points)
    {
      EXPECT_NEAR(point.y, padScale.y, 2.0f * overlap) << "point (" << point.x << ", " << point.z << ")";
    }
  }

  void expectFourBasePointsBothWays(const Shape dominoShape, const Shape padShape, const float yawDegrees)
  {
    const Resting resting(dominoShape, padShape, yawDegrees);

    {
      SCOPED_TRACE("domino first");
      const auto contact = collisions::findContact(*resting.dominoCollider, *resting.padCollider);
      ASSERT_TRUE(contact.has_value());
      EXPECT_NEAR(contact->depth(), overlap, tolerance);
      expectPointsAreTheBase(*contact, resting);
    }

    {
      SCOPED_TRACE("pad first");
      const auto contact = collisions::findContact(*resting.padCollider, *resting.dominoCollider);
      ASSERT_TRUE(contact.has_value());
      EXPECT_NEAR(contact->depth(), overlap, tolerance);
      expectPointsAreTheBase(*contact, resting);
    }
  }
}

TEST(NarrowPhaseFaceManifold, AYawedBoxOnALargerBoxTouchesAtItsFourBaseCorners)
{
  expectFourBasePointsBothWays(Shape::box, Shape::box, 30.0f);
}

TEST(NarrowPhaseFaceManifold, AnUnturnedBoxOnALargerBoxTouchesAtItsFourBaseCorners)
{
  expectFourBasePointsBothWays(Shape::box, Shape::box, 0.0f);
}

TEST(NarrowPhaseFaceManifold, AYawedHullOnALargerBoxTouchesAtItsFourBaseCorners)
{
  expectFourBasePointsBothWays(Shape::hull, Shape::box, 30.0f);
}

TEST(NarrowPhaseFaceManifold, AYawedHullOnALargerHullTouchesAtItsFourBaseCorners)
{
  expectFourBasePointsBothWays(Shape::hull, Shape::hull, 30.0f);
}

TEST(NarrowPhaseFaceManifold, AYawedBoxOnALargerHullTouchesAtItsFourBaseCorners)
{
  expectFourBasePointsBothWays(Shape::box, Shape::hull, 30.0f);
}

TEST(NarrowPhaseFaceManifold, AnUnturnedHullOnALargerHullTouchesAtItsFourBaseCorners)
{
  expectFourBasePointsBothWays(Shape::hull, Shape::hull, 0.0f);
}

namespace {
  using Polygon = std::vector<glm::vec2>;

  constexpr float boxPenetration = 0.01f;
  constexpr float boxHalfHeight = 0.25f;

  // A face-on-face pair: a box at the origin and a yawed one sitting on it. Footprints are the rectangles the boxes
  // cover in the xz plane.
  struct FacePair {
    fixtures::Scene scene;
    std::shared_ptr<Object> lower;
    std::shared_ptr<Object> upper;
    std::shared_ptr<Collider> lowerCollider;
    std::shared_ptr<Collider> upperCollider;

    // offset is in the lower box's frame, which is turned by lowerYawDegrees.
    FacePair(const glm::vec2& lowerHalf, const glm::vec2& upperHalf, const glm::vec2& offset, const float yawDegrees,
             const float lowerYawDegrees = 0.0f)
    {
      lower = addObject(scene, "Lower", { 0, 0, 0 }, { lowerHalf.x, boxHalfHeight, lowerHalf.y });
      transformOf(lower)->setRotation({ 0, lowerYawDegrees, 0 });
      const glm::vec3 placed = transformOf(lower)->getOrientation() * glm::vec3(offset.x, 0.0f, offset.y);
      upper = addObject(scene, "Upper", { placed.x, 2.0f * boxHalfHeight - boxPenetration, placed.z },
                        { upperHalf.x, boxHalfHeight, upperHalf.y });
      transformOf(upper)->setRotation({ 0, yawDegrees, 0 });
      lowerCollider = fixtures::addBoxCollider(lower);
      upperCollider = fixtures::addBoxCollider(upper);
    }
  };

  [[nodiscard]] Polygon footprintOf(const std::shared_ptr<Object>& object)
  {
    const auto transform = transformOf(object);
    const auto half = transform->getScale();

    Polygon corners;
    for (const auto& [x, z] : std::array<std::array<float, 2>, 4>{ { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } } })
    {
      const glm::vec3 world = transform->getPosition() + transform->getOrientation() * glm::vec3(x * half.x, 0, z * half.z);
      corners.emplace_back(world.x, world.z);
    }

    return corners;
  }

  [[nodiscard]] double crossOf(const glm::dvec2& a, const glm::dvec2& b)
  {
    return a.x * b.y - a.y * b.x;
  }

  [[nodiscard]] double areaOf(const Polygon& polygon)
  {
    double twice = 0.0;
    for (size_t i = 0; i < polygon.size(); ++i)
    {
      twice += crossOf(polygon[i], polygon[(i + 1) % polygon.size()]);
    }

    return std::fabs(twice) * 0.5;
  }

  // The overlap of two rectangles: the first clipped by each edge of the second.
  [[nodiscard]] Polygon overlapOf(Polygon subject, Polygon clip)
  {
    if (crossOf(glm::dvec2(clip[1]) - glm::dvec2(clip[0]), glm::dvec2(clip[2]) - glm::dvec2(clip[1])) < 0.0)
    {
      std::ranges::reverse(clip);
    }

    for (size_t i = 0; i < clip.size() && !subject.empty(); ++i)
    {
      const glm::dvec2 origin = clip[i];
      const glm::dvec2 edge = glm::dvec2(clip[(i + 1) % clip.size()]) - origin;
      const auto side = [&](const glm::dvec2& point) { return crossOf(edge, point - origin); };

      Polygon output;
      for (size_t j = 0; j < subject.size(); ++j)
      {
        const glm::dvec2 current = subject[j];
        const glm::dvec2 previous = subject[(j + subject.size() - 1) % subject.size()];
        const double currentSide = side(current);
        const double previousSide = side(previous);

        if ((currentSide >= 0.0) != (previousSide >= 0.0))
        {
          output.emplace_back(previous + (current - previous) * (previousSide / (previousSide - currentSide)));
        }

        if (currentSide >= 0.0)
        {
          output.emplace_back(current);
        }
      }

      subject = output;
    }

    return subject;
  }

  [[nodiscard]] Polygon hullOf(std::vector<glm::vec2> points)
  {
    if (points.size() < 3)
    {
      return points;
    }

    std::ranges::sort(points, [](const glm::vec2& a, const glm::vec2& b) {
      return a.x < b.x || (a.x == b.x && a.y < b.y);
    });

    const auto turn = [](const glm::dvec2& a, const glm::dvec2& b, const glm::dvec2& c) {
      return crossOf(b - a, c - a);
    };

    Polygon hull(2 * points.size());
    size_t size = 0;
    for (size_t i = 0; i < points.size(); ++i)
    {
      while (size >= 2 && turn(hull[size - 2], hull[size - 1], points[i]) <= 0.0)
      {
        --size;
      }
      hull[size++] = points[i];
    }

    for (size_t i = points.size() - 1, lowest = size + 1; i-- > 0;)
    {
      while (size >= lowest && turn(hull[size - 2], hull[size - 1], points[i]) <= 0.0)
      {
        --size;
      }
      hull[size++] = points[i];
    }

    hull.resize(size - 1);

    return hull;
  }

  // The most area four of the polygon's corners can cover, which is all a manifold holds.
  [[nodiscard]] double bestFourCornerArea(const Polygon& polygon)
  {
    const size_t count = polygon.size();
    if (count <= 4)
    {
      return areaOf(polygon);
    }

    double best = 0.0;
    for (size_t a = 0; a < count; ++a)
    {
      for (size_t b = a + 1; b < count; ++b)
      {
        for (size_t c = b + 1; c < count; ++c)
        {
          for (size_t d = c + 1; d < count; ++d)
          {
            best = std::max(best, areaOf(hullOf({ polygon[a], polygon[b], polygon[c], polygon[d] })));
          }
        }
      }
    }

    return best;
  }

  [[nodiscard]] bool insideBox(const glm::vec3& point, const std::shared_ptr<Object>& object, const float slack)
  {
    const auto transform = transformOf(object);
    const glm::vec3 local = glm::inverse(transform->getOrientation()) * (point - transform->getPosition());
    const glm::vec3 half = transform->getScale();

    return std::fabs(local.x) <= half.x + slack && std::fabs(local.y) <= half.y + slack &&
           std::fabs(local.z) <= half.z + slack;
  }

  struct ManifoldCheck {
    bool found = false;
    size_t pointCount = 0;
    bool allInsideBoth = true;
    bool allFinite = true;
    bool allDistinct = true;
    double overlapArea = 0.0;
    double bestFourArea = 0.0;
    double coveredArea = 0.0;
  };

  [[nodiscard]] ManifoldCheck checkManifold(const FacePair& pair, const bool lowerFirst)
  {
    ManifoldCheck check;
    const auto overlapPolygon = overlapOf(footprintOf(pair.upper), footprintOf(pair.lower));
    check.overlapArea = areaOf(overlapPolygon);
    check.bestFourArea = bestFourCornerArea(overlapPolygon);

    const auto contact = lowerFirst ? collisions::findContact(*pair.lowerCollider, *pair.upperCollider)
                                    : collisions::findContact(*pair.upperCollider, *pair.lowerCollider);
    if (!contact)
    {
      return check;
    }

    check.found = true;
    const auto points = contact->contactPoints();
    check.pointCount = points.size();

    constexpr float slack = 2e-3f;
    std::vector<glm::vec2> planar;
    for (size_t i = 0; i < points.size(); ++i)
    {
      const auto& point = points[i];
      check.allFinite = check.allFinite && std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
      for (size_t j = 0; j < i; ++j)
      {
        check.allDistinct = check.allDistinct && glm::distance(point, points[j]) > 1e-5f;
      }

      check.allInsideBoth = check.allInsideBoth && insideBox(point, pair.lower, slack) &&
                            insideBox(point, pair.upper, slack);
      planar.emplace_back(point.x, point.z);
    }

    check.coveredArea = areaOf(hullOf(planar));

    return check;
  }

  [[nodiscard]] float randomIn(std::mt19937& generator, const float low, const float high)
  {
    return std::uniform_real_distribution<float>(low, high)(generator);
  }
}

TEST(NarrowPhaseFaceManifold, TwoThinBoxesCrossingAtAnAngleKeepTheirWholeOverlap)
{
  // Two 4 by 0.4 planks crossing at 60 degrees: the overlap is a rhombus whose corners are all edge crossings.
  const FacePair pair({ 2.0f, 0.2f }, { 2.0f, 0.2f }, { 0.0f, 0.0f }, 60.0f);

  for (const bool lowerFirst : { true, false })
  {
    SCOPED_TRACE(lowerFirst ? "lower first" : "upper first");
    const auto check = checkManifold(pair, lowerFirst);

    ASSERT_TRUE(check.found);
    EXPECT_GT(check.overlapArea, 0.18);
    EXPECT_TRUE(check.allInsideBoth);
    EXPECT_TRUE(check.allFinite);
    EXPECT_TRUE(check.allDistinct);
    EXPECT_EQ(check.pointCount, 4u);
    EXPECT_GE(check.coveredArea, 0.99 * check.overlapArea);
  }
}

TEST(NarrowPhaseFaceManifold, PartlyOverlappingYawedBoxesKeepTheirOverlapInsideBothBoxes)
{
  std::mt19937 generator(20240611);

  int checked = 0;
  int exact = 0;
  for (int trial = 0; trial < 400; ++trial)
  {
    const glm::vec2 lowerHalf(randomIn(generator, 0.1f, 5.0f), randomIn(generator, 0.1f, 5.0f));
    const glm::vec2 upperHalf(randomIn(generator, 0.1f, 5.0f), randomIn(generator, 0.1f, 5.0f));
    const float reach = glm::length(lowerHalf) + glm::length(upperHalf);
    const glm::vec2 offset(randomIn(generator, -0.5f, 0.5f) * reach, randomIn(generator, -0.5f, 0.5f) * reach);
    const FacePair pair(lowerHalf, upperHalf, offset, randomIn(generator, 5.0f, 175.0f));

    for (const bool lowerFirst : { true, false })
    {
      const auto check = checkManifold(pair, lowerFirst);

      // A pair whose footprints barely meet has nothing to cover.
      if (check.overlapArea < 0.05)
      {
        continue;
      }

      SCOPED_TRACE(testing::Message() << "trial " << trial << (lowerFirst ? " lower first" : " upper first"));
      ASSERT_TRUE(check.found);
      ++checked;
      EXPECT_TRUE(check.allInsideBoth);

      // The four points are the corners covering the most area, which is the whole of a quadrilateral overlap. Four
      // corners of a pentagon or larger hold at least about 70% of it (a regular octagon is the worst).
      const bool fitsInFour = check.bestFourArea >= 0.999 * check.overlapArea;
      EXPECT_GE(check.coveredArea, 0.97 * check.bestFourArea);
      EXPECT_GE(check.coveredArea, (fitsInFour ? 0.97 : 0.65) * check.overlapArea);
      EXPECT_TRUE(check.allFinite);
      EXPECT_TRUE(check.allDistinct);
      exact += fitsInFour ? 1 : 0;
    }
  }

  EXPECT_GT(checked, 200);
  EXPECT_GT(exact, 100);
}

namespace {
  struct PlacedBox {
    std::shared_ptr<Object> object;
    std::shared_ptr<Collider> collider;
  };

  PlacedBox placeBox(const fixtures::Scene& scene, const glm::vec3& half, const glm::vec3& eulerDegrees,
                     const glm::vec3& position)
  {
    PlacedBox box;
    box.object = addObject(scene, "Box", position, half);
    transformOf(box.object)->setRotation(eulerDegrees);
    box.collider = fixtures::addBoxCollider(box.object);

    return box;
  }

  // Sets the box's height so its lowest corner is depth below y = top.
  void sinkLowestCornerTo(const PlacedBox& box, const glm::vec3& half, const float top, const float depth)
  {
    const auto transform = transformOf(box.object);

    float lowest = 1e9f;
    for (const float x : { -1.0f, 1.0f })
    {
      for (const float y : { -1.0f, 1.0f })
      {
        for (const float z : { -1.0f, 1.0f })
        {
          lowest = std::min(lowest, (transform->getOrientation() * (glm::vec3(x, y, z) * half)).y);
        }
      }
    }

    transform->setPosition({ 0.0f, top - depth - lowest, 0.0f });
  }

  [[nodiscard]] float distanceToNearest(const glm::vec3& corner, const std::span<const glm::vec3> points)
  {
    float nearest = 1e9f;
    for (const auto& point : points)
    {
      nearest = std::min(nearest, glm::distance(point, corner));
    }

    return nearest;
  }
}

TEST(NarrowPhaseFaceManifold, IdenticalBoxesStackedAtRandomRotationsKeepAllFourCorners)
{
  std::mt19937 generator(7);

  int checked = 0;
  for (int trial = 0; trial < 60; ++trial)
  {
    const glm::vec3 half(randomIn(generator, 0.3f, 2.0f), randomIn(generator, 0.3f, 1.0f),
                         randomIn(generator, 0.3f, 2.0f));
    const glm::vec3 euler(randomIn(generator, 0.0f, 360.0f), randomIn(generator, 0.0f, 360.0f),
                          randomIn(generator, 0.0f, 360.0f));

    const fixtures::Scene scene;
    const auto lower = placeBox(scene, half, euler, { 0, 0, 0 });
    const glm::vec3 up = transformOf(lower.object)->getOrientation() * glm::vec3(0, 1, 0);
    const auto upper = placeBox(scene, half, euler, up * (2.0f * half.y - boxPenetration));

    for (const bool lowerFirst : { true, false })
    {
      SCOPED_TRACE(testing::Message() << "trial " << trial << (lowerFirst ? " lower first" : " upper first"));
      const auto contact = lowerFirst ? collisions::findContact(*lower.collider, *upper.collider)
                                      : collisions::findContact(*upper.collider, *lower.collider);
      ASSERT_TRUE(contact.has_value());

      const auto points = contact->contactPoints();
      EXPECT_EQ(points.size(), 4u);

      const auto transform = transformOf(lower.object);
      for (const float x : { -1.0f, 1.0f })
      {
        for (const float z : { -1.0f, 1.0f })
        {
          const glm::vec3 corner = transform->getPosition() +
                                   transform->getOrientation() * glm::vec3(x * half.x, half.y, z * half.z);
          EXPECT_LT(distanceToNearest(corner, points), 2.0f * boxPenetration)
            << "no point at corner (" << x << ", " << z << ")";
        }
      }

      ++checked;
    }
  }

  EXPECT_EQ(checked, 120);
}

TEST(NarrowPhaseFaceManifold, BoxesSharingAnEdgeLineOverlapInTheirWholeSharedHalf)
{
  // The upper box is shifted one half-width, so its long edges lie on the lower box's side planes, turned off the axes.
  const FacePair pair({ 1.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }, 33.0f, 33.0f);

  for (const bool lowerFirst : { true, false })
  {
    SCOPED_TRACE(lowerFirst ? "lower first" : "upper first");
    const auto check = checkManifold(pair, lowerFirst);

    ASSERT_TRUE(check.found);
    EXPECT_NEAR(check.overlapArea, 2.0, 1e-3);
    EXPECT_EQ(check.pointCount, 4u);
    EXPECT_TRUE(check.allInsideBoth);
    EXPECT_TRUE(check.allDistinct);
    EXPECT_GE(check.coveredArea, 0.99 * check.overlapArea);
  }
}

TEST(NarrowPhaseFaceManifold, ABoxTiltedUnderADegreeOffAFaceKeepsTheTouchingPartOfIt)
{
  // A tall box (so its touching tolerance admits the whole face) turned 0.046 degrees about z: one base edge is 0.0001
  // deep and the face climbs 0.0008 per unit of width, so it leaves the pad's 0.001 tolerance 1.4 units along. The
  // part within it is a polygon of four points, two of them where the face crosses that limit.
  const fixtures::Scene scene;
  const glm::vec3 boxHalf(1.0f, 2.0f, 1.0f);
  const auto pad = placeBox(scene, { 0.9f, 0.25f, 0.9f }, { 0, 0, 0 }, { 0, 0, 0 });
  const auto box = placeBox(scene, boxHalf, { 0, 0, 0.0458f }, { 0, 0, 0 });
  sinkLowestCornerTo(box, boxHalf, 0.25f, 1e-4f);

  // Faces within the tie band make the first collider the reference, so the pad's own tolerance applies.
  const auto contact = collisions::findContact(*pad.collider, *box.collider);
  ASSERT_TRUE(contact.has_value());

  const auto points = contact->contactPoints();
  EXPECT_EQ(points.size(), 4u);

  float low = 1e9f;
  float high = -1e9f;
  for (const auto& point : points)
  {
    low = std::min(low, point.x);
    high = std::max(high, point.x);
  }
  EXPECT_NEAR(low, -0.9f, 0.01f);
  EXPECT_GT(high - low, 1.0f);
}

TEST(NarrowPhaseFaceManifold, AnEdgeRestingOverhangingAFaceKeepsEveryPointInsideBothBoxes)
{
  // A long box turned 20 degrees about its own x axis, so it rests on a bottom edge, and then yawed 35 degrees; the
  // edge is far longer than the pad it crosses.
  const fixtures::Scene scene;
  const glm::vec3 boxHalf(3.0f, 0.5f, 1.0f);
  const auto pad = placeBox(scene, { 1.0f, 0.25f, 1.0f }, { 0, 0, 0 }, { 0, 0, 0 });
  const auto box = placeBox(scene, boxHalf, { 20.0f, 35.0f, 0 }, { 0, 0, 0 });
  sinkLowestCornerTo(box, boxHalf, 0.25f, boxPenetration);

  for (const bool padFirst : { true, false })
  {
    SCOPED_TRACE(padFirst ? "pad first" : "box first");
    const auto contact = padFirst ? collisions::findContact(*pad.collider, *box.collider)
                                  : collisions::findContact(*box.collider, *pad.collider);
    ASSERT_TRUE(contact.has_value());

    const auto points = contact->contactPoints();
    EXPECT_GE(points.size(), 2u);
    for (const auto& point : points)
    {
      EXPECT_TRUE(insideBox(point, pad.object, 2e-3f)) << "outside the pad";
      EXPECT_TRUE(insideBox(point, box.object, 2e-3f)) << "outside the box";
    }
  }
}
