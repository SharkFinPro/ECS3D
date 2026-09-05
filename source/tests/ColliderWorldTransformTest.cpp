#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"

#include <glm/vec3.hpp>
#include <memory>
#include <utility>

namespace {
  // The support function puts every result through a matrix, so its coordinates are compared with a
  // tolerance. The accessors are plain arithmetic and are compared exactly.
  void expectNear(const glm::vec3& actual, const glm::vec3& expected)
  {
    constexpr float tolerance = 1e-5f;

    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }

  // The Object is the collider's lifetime anchor: it holds the Transform the collider resolves through
  // and the raw owner pointer behind it.
  std::pair<std::shared_ptr<Object>, std::shared_ptr<BoxCollider>> makeBox(const glm::vec3& objectScale)
  {
    auto object = std::make_shared<Object>("Collider");
    object->getComponent<Transform>(ComponentType::transform)->setScale(objectScale);

    auto box = std::make_shared<BoxCollider>();
    object->addComponent(box);

    return { object, box };
  }
}

TEST(ColliderWorldTransform, AnUntouchedColliderIsTheSizeOfItsObject)
{
  const auto [object, box] = makeBox(glm::vec3(2, 3, 4));

  // The collider's own scale defaults to 1, which is neutral under the multiply the mesh does. Adding
  // instead would report every axis one larger than the box that actually collides.
  EXPECT_EQ(box->getScale(), glm::vec3(2, 3, 4));
}

TEST(ColliderWorldTransform, TheColliderScaleMultipliesTheObjectScale)
{
  const auto [object, box] = makeBox(glm::vec3(2));
  box->setScale(glm::vec3(3, 1, 0.5f));

  EXPECT_EQ(box->getScale(), glm::vec3(6, 2, 1));
}

TEST(ColliderWorldTransform, PositionAndRotationStillOffsetRatherThanMultiply)
{
  const auto [object, box] = makeBox(glm::vec3(2));
  const auto transform = object->getComponent<Transform>(ComponentType::transform);
  transform->setPosition(glm::vec3(10, 0, 0));
  transform->setRotation(glm::vec3(0, 90, 0));

  box->setPosition(glm::vec3(1, 2, 3));
  box->setRotation(glm::vec3(0, 45, 0));

  EXPECT_EQ(box->getPosition(), glm::vec3(11, 2, 3));
  EXPECT_EQ(box->getRotation(), glm::vec3(0, 135, 0));
}

TEST(ColliderWorldTransform, TheReportedScaleIsTheOneTheCollisionMeshUses)
{
  const auto [object, box] = makeBox(glm::vec3(2, 3, 4));
  box->setScale(glm::vec3(1.5f, 1, 1));

  // The mesh is the unit box [-1,1]^3 through the collider's world matrix, so a corner of it sits at the
  // world scale on every axis. This is the pairing the accessor got wrong: a consumer that trusted
  // getScale() sized the box differently from the geometry GJK actually walks.
  //
  // The directions are tilted off the axes on purpose. An axis-aligned direction leaves four corners
  // tied on the dot product, so the answer would pin the vertex iteration order rather than the math.
  const auto worldScale = box->getScale();

  expectNear(box->findFurthestPoint({ 1.0f, 0.1f, 0.1f }), worldScale);
  expectNear(box->findFurthestPoint({ -1.0f, 0.1f, 0.1f }),
             glm::vec3(-worldScale.x, worldScale.y, worldScale.z));
}
