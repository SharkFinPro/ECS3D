#include <gtest/gtest.h>

#include "ObjectManagerFixtures.h"
#include "TestScene.h"
#include "bindings/BindingContext.h"
#include "bindings/ColliderBindings.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <glm/vec3.hpp>
#include <limits>
#include <memory>
#include <string>
#include <uuid.h>

namespace {
  // Builds a scene with one box-collider object, one sphere-collider object, and one object with no
  // collider at all, and points BindingContext at it - the same setup ScriptSystem does each tick.
  class ColliderBindingsTest : public testing::Test {
  protected:
    void SetUp() override
    {
      m_scene = fixtures::makeScene();

      m_boxObject = fixtures::addObject(m_scene, "Box");
      m_box = fixtures::addBoxCollider(m_boxObject);

      m_sphereObject = fixtures::addObject(m_scene, "Sphere");
      m_sphere = fixtures::addSphereCollider(m_sphereObject, 1.0f);

      m_bareObject = fixtures::addObject(m_scene, "Bare");

      BindingContext::setObjectManager(m_scene.objectManager.get());

      m_bindings = ColliderBindingsProvider::getBindings();
    }

    void TearDown() override
    {
      // Drains whatever this test queued, so the next test's edit-count assertions start from empty.
      static_cast<void>(BindingContext::takeComponentEdits());
      BindingContext::setObjectManager(nullptr);
    }

    static std::string uuidOf(const std::shared_ptr<Object>& object)
    {
      return uuids::to_string(object->getUUID());
    }

    fixtures::Scene m_scene;
    std::shared_ptr<Object> m_boxObject;
    std::shared_ptr<BoxCollider> m_box;
    std::shared_ptr<Object> m_sphereObject;
    std::shared_ptr<SphereCollider> m_sphere;
    std::shared_ptr<Object> m_bareObject;
    ColliderBindings m_bindings{};
  };
}

TEST_F(ColliderBindingsTest, GetShapeNamesEachColliderAndNoneOtherwise)
{
  EXPECT_EQ(m_bindings.getShape(uuidOf(m_boxObject).c_str()), 1);
  EXPECT_EQ(m_bindings.getShape(uuidOf(m_sphereObject).c_str()), 2);

  // Positive control alongside the negative cases below: a real box and a real sphere each report their
  // own shape, so "0" for the other cases reflects an absent collider rather than a broken query.
  EXPECT_EQ(m_bindings.getShape(uuidOf(m_bareObject).c_str()), 0);
  EXPECT_EQ(m_bindings.getShape("not-a-uuid"), 0);
  EXPECT_EQ(m_bindings.getShape(uuids::to_string(objectManagerFixtures::unknownUUID()).c_str()), 0);
}

TEST_F(ColliderBindingsTest, HasIsTrueOnlyForARealCollider)
{
  EXPECT_TRUE(m_bindings.has(uuidOf(m_boxObject).c_str()));
  EXPECT_TRUE(m_bindings.has(uuidOf(m_sphereObject).c_str()));

  EXPECT_FALSE(m_bindings.has(uuidOf(m_bareObject).c_str()));
  EXPECT_FALSE(m_bindings.has("not-a-uuid"));
  EXPECT_FALSE(m_bindings.has(uuids::to_string(objectManagerFixtures::unknownUUID()).c_str()));
}

TEST_F(ColliderBindingsTest, CommonFieldsRoundTripThroughEitherShape)
{
  const auto boxUUID = uuidOf(m_boxObject);

  EXPECT_FALSE(m_bindings.getIsTrigger(boxUUID.c_str()));
  EXPECT_TRUE(m_bindings.setIsTrigger(boxUUID.c_str(), true));
  EXPECT_TRUE(m_bindings.getIsTrigger(boxUUID.c_str()));
  EXPECT_TRUE(m_box->isTrigger());

  EXPECT_EQ(m_bindings.getLayer(boxUUID.c_str()), 0u);
  EXPECT_TRUE(m_bindings.setLayer(boxUUID.c_str(), 5u));
  EXPECT_EQ(m_bindings.getLayer(boxUUID.c_str()), 5u);
  EXPECT_EQ(m_box->getLayer(), 5u);

  // The setter's own clamp (Collider::setLayer) still applies through the binding.
  EXPECT_TRUE(m_bindings.setLayer(boxUUID.c_str(), 999u));
  EXPECT_EQ(m_bindings.getLayer(boxUUID.c_str()), 31u);

  EXPECT_EQ(m_bindings.getMask(boxUUID.c_str()), 0xFFFFFFFFu);
  EXPECT_TRUE(m_bindings.setMask(boxUUID.c_str(), 0x1u));
  EXPECT_EQ(m_bindings.getMask(boxUUID.c_str()), 0x1u);
  EXPECT_EQ(m_box->getMask(), 0x1u);
}

TEST_F(ColliderBindingsTest, CommonFieldsOnAnObjectWithNoColliderAreNeutralAndRefuseToSet)
{
  const auto bareUUID = uuidOf(m_bareObject);

  EXPECT_FALSE(m_bindings.getIsTrigger(bareUUID.c_str()));
  EXPECT_EQ(m_bindings.getLayer(bareUUID.c_str()), 0u);
  EXPECT_EQ(m_bindings.getMask(bareUUID.c_str()), 0u);

  EXPECT_FALSE(m_bindings.setIsTrigger(bareUUID.c_str(), true));
  EXPECT_FALSE(m_bindings.setLayer(bareUUID.c_str(), 3u));
  EXPECT_FALSE(m_bindings.setMask(bareUUID.c_str(), 0x1u));

  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
}

TEST_F(ColliderBindingsTest, BoxOffsetAndSizeRoundTripAndIgnoreNonFiniteInput)
{
  const auto boxUUID = uuidOf(m_boxObject);

  float x = -1, y = -1, z = -1;
  ASSERT_TRUE(m_bindings.getBoxOffset(boxUUID.c_str(), &x, &y, &z));
  EXPECT_FLOAT_EQ(x, 0.0f);
  EXPECT_FLOAT_EQ(y, 0.0f);
  EXPECT_FLOAT_EQ(z, 0.0f);

  EXPECT_TRUE(m_bindings.setBoxOffset(boxUUID.c_str(), 1.0f, 2.0f, 3.0f));
  ASSERT_TRUE(m_bindings.getBoxOffset(boxUUID.c_str(), &x, &y, &z));
  EXPECT_FLOAT_EQ(x, 1.0f);
  EXPECT_FLOAT_EQ(y, 2.0f);
  EXPECT_FLOAT_EQ(z, 3.0f);

  // BoxCollider::setPosition drops non-finite input; the offset from just above must survive untouched.
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  EXPECT_TRUE(m_bindings.setBoxOffset(boxUUID.c_str(), nan, 9.0f, 9.0f));
  ASSERT_TRUE(m_bindings.getBoxOffset(boxUUID.c_str(), &x, &y, &z));
  EXPECT_FLOAT_EQ(x, 1.0f);
  EXPECT_FLOAT_EQ(y, 2.0f);
  EXPECT_FLOAT_EQ(z, 3.0f);

  ASSERT_TRUE(m_bindings.getBoxSize(boxUUID.c_str(), &x, &y, &z));
  EXPECT_FLOAT_EQ(x, 1.0f);
  EXPECT_FLOAT_EQ(y, 1.0f);
  EXPECT_FLOAT_EQ(z, 1.0f);

  EXPECT_TRUE(m_bindings.setBoxSize(boxUUID.c_str(), 2.0f, 3.0f, 4.0f));
  ASSERT_TRUE(m_bindings.getBoxSize(boxUUID.c_str(), &x, &y, &z));
  EXPECT_FLOAT_EQ(x, 2.0f);
  EXPECT_FLOAT_EQ(y, 3.0f);
  EXPECT_FLOAT_EQ(z, 4.0f);
}

TEST_F(ColliderBindingsTest, SphereOffsetAndRadiusRoundTripAndIgnoreNonFiniteInput)
{
  const auto sphereUUID = uuidOf(m_sphereObject);

  float x = -1, y = -1, z = -1;
  ASSERT_TRUE(m_bindings.getSphereOffset(sphereUUID.c_str(), &x, &y, &z));
  EXPECT_FLOAT_EQ(x, 0.0f);
  EXPECT_FLOAT_EQ(y, 0.0f);
  EXPECT_FLOAT_EQ(z, 0.0f);

  EXPECT_TRUE(m_bindings.setSphereOffset(sphereUUID.c_str(), 4.0f, 5.0f, 6.0f));
  ASSERT_TRUE(m_bindings.getSphereOffset(sphereUUID.c_str(), &x, &y, &z));
  EXPECT_FLOAT_EQ(x, 4.0f);
  EXPECT_FLOAT_EQ(y, 5.0f);
  EXPECT_FLOAT_EQ(z, 6.0f);

  float radius = -1;
  ASSERT_TRUE(m_bindings.getSphereRadius(sphereUUID.c_str(), &radius));
  EXPECT_FLOAT_EQ(radius, 1.0f);

  EXPECT_TRUE(m_bindings.setSphereRadius(sphereUUID.c_str(), 2.5f));
  ASSERT_TRUE(m_bindings.getSphereRadius(sphereUUID.c_str(), &radius));
  EXPECT_FLOAT_EQ(radius, 2.5f);

  // SphereCollider::setRadius drops non-finite input the same way the box setters do.
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  EXPECT_TRUE(m_bindings.setSphereRadius(sphereUUID.c_str(), nan));
  ASSERT_TRUE(m_bindings.getSphereRadius(sphereUUID.c_str(), &radius));
  EXPECT_FLOAT_EQ(radius, 2.5f);
}

TEST_F(ColliderBindingsTest, BoxAccessorsFailSafelyOnASphereCollider)
{
  const auto sphereUUID = uuidOf(m_sphereObject);
  const auto boxUUID = uuidOf(m_boxObject);

  float x = -7, y = -7, z = -7;
  EXPECT_FALSE(m_bindings.getBoxOffset(sphereUUID.c_str(), &x, &y, &z));
  // Out params are left untouched on a refused get.
  EXPECT_FLOAT_EQ(x, -7.0f);
  EXPECT_FLOAT_EQ(y, -7.0f);
  EXPECT_FLOAT_EQ(z, -7.0f);

  EXPECT_FALSE(m_bindings.setBoxOffset(sphereUUID.c_str(), 1.0f, 1.0f, 1.0f));
  EXPECT_FALSE(m_bindings.getBoxSize(sphereUUID.c_str(), &x, &y, &z));
  EXPECT_FALSE(m_bindings.setBoxSize(sphereUUID.c_str(), 1.0f, 1.0f, 1.0f));

  // The sphere's own state is untouched by the refused box calls above.
  EXPECT_EQ(m_sphere->getLocalPosition(), glm::vec3(0));
  EXPECT_FLOAT_EQ(m_sphere->getLocalRadius(), 1.0f);

  // Positive control: the same accessor pair succeeds on the collider it actually matches.
  EXPECT_TRUE(m_bindings.getBoxOffset(boxUUID.c_str(), &x, &y, &z));
  EXPECT_TRUE(m_bindings.setBoxOffset(boxUUID.c_str(), 1.0f, 1.0f, 1.0f));
}

TEST_F(ColliderBindingsTest, SphereAccessorsFailSafelyOnABoxCollider)
{
  const auto boxUUID = uuidOf(m_boxObject);
  const auto sphereUUID = uuidOf(m_sphereObject);

  float x = -7, y = -7, z = -7;
  EXPECT_FALSE(m_bindings.getSphereOffset(boxUUID.c_str(), &x, &y, &z));
  EXPECT_FLOAT_EQ(x, -7.0f);
  EXPECT_FLOAT_EQ(y, -7.0f);
  EXPECT_FLOAT_EQ(z, -7.0f);

  EXPECT_FALSE(m_bindings.setSphereOffset(boxUUID.c_str(), 1.0f, 1.0f, 1.0f));

  float radius = -7;
  EXPECT_FALSE(m_bindings.getSphereRadius(boxUUID.c_str(), &radius));
  EXPECT_FLOAT_EQ(radius, -7.0f);
  EXPECT_FALSE(m_bindings.setSphereRadius(boxUUID.c_str(), 9.0f));

  // The box's own state is untouched by the refused sphere calls above.
  EXPECT_EQ(m_box->getLocalPosition(), glm::vec3(0));
  EXPECT_EQ(m_box->getLocalScale(), glm::vec3(1));

  // Positive control: the same accessor pair succeeds on the collider it actually matches.
  EXPECT_TRUE(m_bindings.getSphereRadius(sphereUUID.c_str(), &radius));
  EXPECT_TRUE(m_bindings.setSphereRadius(sphereUUID.c_str(), 2.0f));
}

TEST_F(ColliderBindingsTest, EachSuccessfulSetRecordsExactlyOneComponentEdit)
{
  const auto boxUUID = uuidOf(m_boxObject);

  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());

  EXPECT_TRUE(m_bindings.setBoxOffset(boxUUID.c_str(), 1.0f, 0.0f, 0.0f));

  const auto edits = BindingContext::takeComponentEdits();
  ASSERT_EQ(edits.size(), 1u);
  EXPECT_EQ(edits[0].first, m_boxObject->getUUID());
  EXPECT_EQ(edits[0].second, m_box);

  // Draining above emptied the buffer, so the getter/refused-set cases that follow start from empty.
  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
}

TEST_F(ColliderBindingsTest, AGetOrARefusedSetRecordsNoComponentEdit)
{
  const auto boxUUID = uuidOf(m_boxObject);
  const auto sphereUUID = uuidOf(m_sphereObject);
  const auto bareUUID = uuidOf(m_bareObject);

  float x = 0, y = 0, z = 0, radius = 0;
  static_cast<void>(m_bindings.getBoxOffset(boxUUID.c_str(), &x, &y, &z));
  static_cast<void>(m_bindings.getIsTrigger(boxUUID.c_str()));
  static_cast<void>(m_bindings.getLayer(boxUUID.c_str()));
  static_cast<void>(m_bindings.getMask(boxUUID.c_str()));

  // Refused: wrong shape, missing collider, unknown uuid.
  static_cast<void>(m_bindings.setSphereRadius(boxUUID.c_str(), 2.0f));
  static_cast<void>(m_bindings.setBoxOffset(sphereUUID.c_str(), 1.0f, 1.0f, 1.0f));
  static_cast<void>(m_bindings.setIsTrigger(bareUUID.c_str(), true));
  static_cast<void>(m_bindings.setLayer("not-a-uuid", 1u));
  static_cast<void>(m_bindings.getSphereRadius(bareUUID.c_str(), &radius));

  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
}
