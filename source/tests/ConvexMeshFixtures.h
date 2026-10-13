#ifndef CONVEXMESHFIXTURES_H
#define CONVEXMESHFIXTURES_H

#include "TestPrinters.h"
#include "TestScene.h"
#include "objects/components/collisions/ConvexHull.h"
#include "objects/components/collisions/ConvexHullValidation.h"
#include "objects/components/collisions/ConvexMeshCollider.h"

#include <gtest/gtest.h>
#include <glm/vec3.hpp>
#include <memory>
#include <vector>
#include <uuid.h>

// Shared by the convex mesh collider suites.
namespace convexFixtures {
  // Looser than the fixture default: these coordinates come out of a matrix.
  inline void expectNear(const glm::vec3& actual, const glm::vec3& expected)
  {
    fixtures::expectNear(actual, expected, 1e-4f);
  }

  inline void expectNear(const char* what, const glm::vec3& actual, const glm::vec3& expected, const float tolerance = 1e-4f)
  {
    fixtures::expectNear(what, actual, expected, tolerance);
  }

  inline const uuids::uuid sourceModel = uuids::uuid::from_string("11111111-2222-3333-4444-555555555555").value();

  // Apex above a base triangle: a hull that is not the default cube.
  inline ConvexHull tetrahedron()
  {
    const std::vector<glm::vec3> points{ { -1, -1, -1 }, { 1, -1, -1 }, { 0, -1, 1 }, { 0, 1, 0 } };

    return *buildConvexHull(points);
  }

  inline std::shared_ptr<ConvexMeshCollider> customCollider()
  {
    auto collider = std::make_shared<ConvexMeshCollider>();
    EXPECT_TRUE(collider->setHull(tetrahedron()));
    collider->setSourceModel(sourceModel);
    collider->setPosition({ 1.0f, 2.0f, 3.0f });
    collider->setRotation({ 10.0f, 20.0f, 30.0f });
    collider->setScale({ 0.5f, 1.5f, 2.5f });
    collider->setRenderCollider(true);
    collider->setIsTrigger(true);
    collider->setLayer(5);
    collider->setMask(0xF0u);

    return collider;
  }

  inline void expectSameCollider(ConvexMeshCollider& actual, ConvexMeshCollider& expected)
  {
    EXPECT_EQ(actual.getHull().vertices, expected.getHull().vertices);
    EXPECT_EQ(actual.getHull().indices, expected.getHull().indices);
    EXPECT_EQ(actual.getSourceModel(), expected.getSourceModel());
    EXPECT_EQ(actual.getLocalPosition(), expected.getLocalPosition());
    EXPECT_EQ(actual.getLocalRotation(), expected.getLocalRotation());
    EXPECT_EQ(actual.getLocalScale(), expected.getLocalScale());
    EXPECT_EQ(actual.getRenderCollider(), expected.getRenderCollider());
    EXPECT_EQ(actual.isTrigger(), expected.isTrigger());
    EXPECT_EQ(actual.getLayer(), expected.getLayer());
    EXPECT_EQ(actual.getMask(), expected.getMask());
  }
}

#endif //CONVEXMESHFIXTURES_H
