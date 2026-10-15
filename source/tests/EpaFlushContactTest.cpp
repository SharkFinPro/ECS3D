#include <gtest/gtest.h>

#include "TestScene.h"
#include "collisions/NarrowPhase.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <cmath>

namespace {
  // A body box lying flush on a ground box, both tilted by tiltDegrees about z and scaled by scale, sunk into
  // it by a tenth of its half-size. The body must be moved out along the tilted up axis by exactly that.
  void expectFlushContact(const float scale, const float tiltDegrees)
  {
    const auto scene = fixtures::makeScene();

    const auto ground = fixtures::addObject(scene, "Ground", { 0, 0, 0 });
    fixtures::transformOf(ground)->setScale({ 10 * scale, scale, 10 * scale });
    fixtures::transformOf(ground)->setRotation({ 0, 0, tiltDegrees });
    const auto groundCollider = fixtures::addBoxCollider(ground);

    const float angle = glm::radians(tiltDegrees);
    const glm::vec3 up{ -std::sin(angle), std::cos(angle), 0.0f };
    const auto body = fixtures::addObject(scene, "Body", up * (1.9f * scale));
    fixtures::transformOf(body)->setScale({ scale, scale, scale });
    fixtures::transformOf(body)->setRotation({ 0, 0, tiltDegrees });
    const auto bodyCollider = fixtures::addBoxCollider(body);

    const auto contact = collisions::findContact(*bodyCollider, *groundCollider);
    ASSERT_TRUE(contact.has_value());

    const glm::vec3 expected = up * (0.1f * scale);
    EXPECT_LT(glm::length(contact->minimumTranslationVector - expected), 2e-3f * scale);
  }

  // A body box stood on a corner, its body diagonal straight down, sunk a tenth of its half-size into a flat
  // ground box. The body must be moved straight up by exactly that.
  void expectCornerContact(const float scale)
  {
    const auto scene = fixtures::makeScene();

    const auto ground = fixtures::addObject(scene, "Ground", { 0, 0, 0 });
    fixtures::transformOf(ground)->setScale({ 10 * scale, scale, 10 * scale });
    const auto groundCollider = fixtures::addBoxCollider(ground);

    const float centerY = scale + std::sqrt(3.0f) * scale - 0.1f * scale;
    const auto body = fixtures::addObject(scene, "Body", { 0, centerY, 0 });
    fixtures::transformOf(body)->setScale({ scale, scale, scale });
    fixtures::transformOf(body)->setRotation({ 45, 0, 35.26439f });
    const auto bodyCollider = fixtures::addBoxCollider(body);

    const auto contact = collisions::findContact(*bodyCollider, *groundCollider);
    ASSERT_TRUE(contact.has_value());

    EXPECT_LT(glm::length(contact->minimumTranslationVector - glm::vec3{ 0, 0.1f * scale, 0 }), 2e-3f * scale);
  }
}

TEST(EpaFlushContact, AFlatBoxPairReportsTheFaceNormalTimesTheDepthAtUnitScale)
{
  expectFlushContact(1.0f, 0.0f);
}

TEST(EpaFlushContact, AFlatBoxPairReportsTheFaceNormalTimesTheDepthAtLargeScale)
{
  expectFlushContact(100.0f, 0.0f);
}

TEST(EpaFlushContact, AFlatBoxPairReportsTheFaceNormalTimesTheDepthAtSmallScale)
{
  expectFlushContact(0.01f, 0.0f);
}

TEST(EpaFlushContact, ATiltedBoxPairReportsTheFaceNormalTimesTheDepthAtEachScale)
{
  expectFlushContact(1.0f, 5.71059f);
  expectFlushContact(100.0f, 5.71059f);
  expectFlushContact(0.01f, 5.71059f);
}

TEST(EpaFlushContact, ABoxStoodOnACornerReportsTheDepthStraightUpAtEachScale)
{
  expectCornerContact(1.0f);
  expectCornerContact(100.0f);
  expectCornerContact(0.01f);
}
