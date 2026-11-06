#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "collisions/NarrowPhase.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/Collider.h"
#include "objects/components/collisions/ConvexMeshCollider.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <random>
#include <vector>

namespace {
  // A collider on its own object, with the box's world corners and unit axes rebuilt the way BoxCollider builds its
  // mesh, so the reference below sees the same geometry the narrow phase does.
  struct Body {
    std::shared_ptr<Object> object;
    std::shared_ptr<Collider> collider;
    std::array<glm::dvec3, 8> corners{};
    std::array<glm::dvec3, 3> axes{};
  };

  template <typename T>
  Body makeBody(const glm::vec3& position, const glm::vec3& scale, const glm::vec3& rotation)
  {
    auto object = std::make_shared<Object>("Body");
    const auto transform = object->getComponent<Transform>(ComponentType::transform);
    transform->setPosition(position);
    transform->setScale(scale);
    transform->setRotation(rotation);

    auto collider = std::make_shared<T>();
    object->addComponent(collider);

    const auto turn = glm::rotate(glm::mat4(1.0f), glm::radians(rotation.z), { 0, 0, 1 }) *
                      glm::rotate(glm::mat4(1.0f), glm::radians(rotation.y), { 0, 1, 0 }) *
                      glm::rotate(glm::mat4(1.0f), glm::radians(rotation.x), { 1, 0, 0 });
    const auto world = glm::translate(glm::mat4(1.0f), position) * turn * glm::scale(glm::mat4(1.0f), scale);

    Body body{ object, collider };
    for (size_t i = 0; i < boxVertices.size(); ++i)
    {
      body.corners[i] = glm::dvec3(glm::vec3(world * glm::vec4(boxVertices[i], 1.0f)));
    }
    for (int axis = 0; axis < 3; ++axis)
    {
      body.axes[axis] = glm::normalize(glm::dvec3(glm::vec3(turn[axis])));
    }

    return body;
  }

  // How far the two corner sets overlap along axis.
  double overlapAlong(const Body& first, const Body& second, const glm::dvec3& axis)
  {
    double firstLow = std::numeric_limits<double>::max();
    double firstHigh = std::numeric_limits<double>::lowest();
    double secondLow = firstLow;
    double secondHigh = firstHigh;
    for (const auto& corner : first.corners)
    {
      firstLow = std::min(firstLow, glm::dot(corner, axis));
      firstHigh = std::max(firstHigh, glm::dot(corner, axis));
    }
    for (const auto& corner : second.corners)
    {
      secondLow = std::min(secondLow, glm::dot(corner, axis));
      secondHigh = std::max(secondHigh, glm::dot(corner, axis));
    }

    return std::min(firstHigh - secondLow, secondHigh - firstLow);
  }

  // The exact penetration depth of two boxes: the least overlap over their 15 separating axes.
  double separatingAxisDepth(const Body& first, const Body& second)
  {
    std::vector<glm::dvec3> axes(first.axes.begin(), first.axes.end());
    axes.insert(axes.end(), second.axes.begin(), second.axes.end());
    for (const auto& a : first.axes)
    {
      for (const auto& b : second.axes)
      {
        if (const auto crossed = glm::cross(a, b); glm::length(crossed) > 1e-9)
        {
          axes.push_back(glm::normalize(crossed));
        }
      }
    }

    double depth = std::numeric_limits<double>::max();
    for (const auto& axis : axes)
    {
      depth = std::min(depth, overlapAlong(first, second, axis));
    }

    return depth;
  }

  enum class Arrangement { stackedTurnedAlike, stackedTurnedApart, anyhow, flush };

  struct Case {
    Arrangement arrangement;
    glm::vec3 lowerScale;
    glm::vec3 upperScale;
    glm::vec3 lowerRotation;
    glm::vec3 upperRotation;
    glm::vec3 lowerPosition;
    glm::vec3 upperPosition;
  };

  // Random pairs of boxes: stacked as on a slope, overlapping by up to a fifth of their height and often barely,
  // exactly flush, or anyhow.
  std::vector<Case> randomCases(const int count)
  {
    std::mt19937 random(1234);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const auto scale = [&] { return glm::vec3(0.3f + 2.0f * unit(random), 0.3f + 2.0f * unit(random), 0.3f + 2.0f * unit(random)); };
    const auto anyRotation = [&] { return glm::vec3(360.0f * unit(random), 360.0f * unit(random), 360.0f * unit(random)); };

    std::vector<Case> cases;
    for (int n = 0; n < count; ++n)
    {
      Case pair{ static_cast<Arrangement>(n % 4), scale(), scale() };
      const float tilt = 30.0f * unit(random);
      pair.lowerRotation = pair.arrangement == Arrangement::anyhow ? anyRotation() : glm::vec3(0, 0, tilt);
      pair.upperRotation = pair.arrangement == Arrangement::anyhow           ? anyRotation()
                           : pair.arrangement == Arrangement::stackedTurnedApart ? glm::vec3(0, 0, tilt + 2.0f * unit(random) - 1.0f)
                                                                              : pair.lowerRotation;
      pair.lowerPosition = glm::vec3(40.0f * unit(random) - 20.0f, 5.0f * unit(random), 10.0f * unit(random) - 5.0f);

      const auto lower = makeBody<BoxCollider>(pair.lowerPosition, pair.lowerScale, pair.lowerRotation);
      if (pair.arrangement == Arrangement::anyhow)
      {
        pair.upperPosition = pair.lowerPosition + glm::vec3(2 * unit(random) - 1, 2 * unit(random) - 1, 2 * unit(random) - 1) *
                                                    (pair.lowerScale + pair.upperScale) * 0.6f;
      }
      else
      {
        const float overlap = pair.arrangement == Arrangement::flush
                                ? 0.0f
                                : (pair.lowerScale.y + pair.upperScale.y) * 0.2f * std::pow(unit(random), 4.0f);
        pair.upperPosition = pair.lowerPosition + glm::vec3(lower.axes[1]) * (pair.lowerScale.y + pair.upperScale.y - overlap) +
                             glm::vec3(lower.axes[0]) * (pair.lowerScale.x * (2 * unit(random) - 1)) +
                             glm::vec3(lower.axes[2]) * (pair.lowerScale.z * (2 * unit(random) - 1));
      }

      cases.push_back(pair);
    }

    return cases;
  }

  // The narrow phase's depth may differ from the exact one by float noise in the corners, no more.
  constexpr double depthTolerance = 1e-4;

  // Below this the pair only touches, as far as float corners can tell.
  constexpr double touching = 1e-5;
}

// EPA's depth has to be the true overlap whatever pair it meets, from either side, and two boxes that only touch must
// not be pushed apart.
TEST(EpaConvergence, BoxPairDepthsMatchTheSeparatingAxes)
{
  int overlapping = 0;
  for (const auto& pair : randomCases(4000))
  {
    const auto lower = makeBody<BoxCollider>(pair.lowerPosition, pair.lowerScale, pair.lowerRotation);
    const auto upper = makeBody<BoxCollider>(pair.upperPosition, pair.upperScale, pair.upperRotation);
    const double reference = separatingAxisDepth(lower, upper);
    overlapping += reference > touching ? 1 : 0;

    for (const bool upperFirst : { true, false })
    {
      SCOPED_TRACE(testing::Message() << "arrangement " << static_cast<int>(pair.arrangement) << ", upper first "
                                      << upperFirst << ", reference depth " << reference);

      const auto contact = upperFirst ? collisions::findContact(*upper.collider, *lower.collider)
                                      : collisions::findContact(*lower.collider, *upper.collider);

      if (contact)
      {
        ASSERT_LE(contact->depth(), std::max(reference, 0.0) + depthTolerance);
      }

      if (reference > touching)
      {
        ASSERT_TRUE(contact.has_value());
        ASSERT_NEAR(contact->depth(), reference, depthTolerance);
      }
    }
  }

  // Positive control: most of the pairs overlap, so the checks above are not vacuous.
  EXPECT_GT(overlapping, 2500);
}

// A hull built as the same box lists its corners in another order, so its support breaks ties toward other corners.
// The translation out of the overlap must not depend on that.
TEST(EpaConvergence, AHullOfTheSameBoxGivesTheSameTranslation)
{
  int compared = 0;
  for (const auto& pair : randomCases(2000))
  {
    const auto lower = makeBody<BoxCollider>(pair.lowerPosition, pair.lowerScale, pair.lowerRotation);
    const auto box = makeBody<BoxCollider>(pair.upperPosition, pair.upperScale, pair.upperRotation);
    const auto hull = makeBody<ConvexMeshCollider>(pair.upperPosition, pair.upperScale, pair.upperRotation);

    const auto fromBox = collisions::findContact(*box.collider, *lower.collider);
    const auto fromHull = collisions::findContact(*hull.collider, *lower.collider);

    SCOPED_TRACE(testing::Message() << "arrangement " << static_cast<int>(pair.arrangement));

    // Within the touching band, which scales with the support point measured, one may call the pair touching where
    // the other reports a contact of that depth; neither is wrong by more than the band.
    if (fromBox.has_value() != fromHull.has_value())
    {
      ASSERT_LE((fromBox ? fromBox : fromHull)->depth(), touching);
      continue;
    }

    if (fromBox)
    {
      ++compared;
      ASSERT_LE(glm::distance(fromBox->minimumTranslationVector, fromHull->minimumTranslationVector), depthTolerance);
    }
  }

  EXPECT_GT(compared, 1000);
}
