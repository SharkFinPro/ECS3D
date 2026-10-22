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
#include <glm/vec3.hpp>
#include <array>
#include <memory>

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
