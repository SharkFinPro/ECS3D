#include <gtest/gtest.h>

#include "ObjectManagerFixtures.h"
#include "TestScene.h"
#include "bindings/BindingContext.h"
#include "bindings/ColliderBindings.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/CapsuleCollider.h"
#include "objects/components/collisions/ConvexHull.h"
#include "objects/components/collisions/ConvexMeshCollider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <glm/vec3.hpp>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <uuid.h>
#include <vector>

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

      m_capsuleObject = fixtures::addObject(m_scene, "Capsule");
      m_capsule = fixtures::addCapsuleCollider(m_capsuleObject, 0.5f, 2.0f);

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
    std::shared_ptr<Object> m_capsuleObject;
    std::shared_ptr<CapsuleCollider> m_capsule;
    std::shared_ptr<Object> m_bareObject;
    ColliderBindings m_bindings{};
  };
}

TEST_F(ColliderBindingsTest, GetShapeNamesEachColliderAndNoneOtherwise)
{
  EXPECT_EQ(m_bindings.getShape(uuidOf(m_boxObject).c_str()), 1);
  EXPECT_EQ(m_bindings.getShape(uuidOf(m_sphereObject).c_str()), 2);
  EXPECT_EQ(m_bindings.getShape(uuidOf(m_capsuleObject).c_str()), 3);

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
  EXPECT_TRUE(m_bindings.has(uuidOf(m_capsuleObject).c_str()));

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

TEST_F(ColliderBindingsTest, ANonFiniteSetIsAcceptedButRecordsNoComponentEdit)
{
  const auto boxUUID = uuidOf(m_boxObject);
  const auto sphereUUID = uuidOf(m_sphereObject);
  const auto nan = std::numeric_limits<float>::quiet_NaN();

  // The component setter behind each of these silently drops non-finite input and leaves its value
  // untouched, so the binding must not queue a no-op edit even though it returns true (shape matched).
  EXPECT_TRUE(m_bindings.setBoxOffset(boxUUID.c_str(), nan, 0.0f, 0.0f));
  EXPECT_TRUE(m_bindings.setBoxSize(boxUUID.c_str(), nan, 1.0f, 1.0f));
  EXPECT_TRUE(m_bindings.setSphereOffset(sphereUUID.c_str(), nan, 0.0f, 0.0f));
  EXPECT_TRUE(m_bindings.setSphereRadius(sphereUUID.c_str(), nan));

  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());

  // Positive control: the same accessors with finite input on the same objects do record, so the empty
  // result above reflects the non-finite no-op rather than these setters never recording at all.
  EXPECT_TRUE(m_bindings.setBoxOffset(boxUUID.c_str(), 1.0f, 0.0f, 0.0f));
  EXPECT_FALSE(BindingContext::takeComponentEdits().empty());
}

TEST_F(ColliderBindingsTest, EveryFiniteSetterRecordsExactlyOneEditForItsComponent)
{
  const auto hullObject = fixtures::addObject(m_scene, "Hull");
  const auto hull = std::make_shared<ConvexMeshCollider>();
  hullObject->addComponent(hull);

  const auto box = uuidOf(m_boxObject);
  const auto sphere = uuidOf(m_sphereObject);
  const auto capsule = uuidOf(m_capsuleObject);
  const auto hullUUID = uuidOf(hullObject);

  struct Case {
    const char* name;
    std::function<bool()> set;
    uuids::uuid object;
    std::shared_ptr<Component> component;
  };
  const std::vector<Case> cases = {
    { "setIsTrigger", [&] { return m_bindings.setIsTrigger(box.c_str(), true); }, m_boxObject->getUUID(), m_box },
    { "setLayer", [&] { return m_bindings.setLayer(box.c_str(), 5u); }, m_boxObject->getUUID(), m_box },
    { "setMask", [&] { return m_bindings.setMask(box.c_str(), 0x1u); }, m_boxObject->getUUID(), m_box },
    { "setBoxOffset", [&] { return m_bindings.setBoxOffset(box.c_str(), 1.0f, 2.0f, 3.0f); },
      m_boxObject->getUUID(), m_box },
    { "setBoxSize", [&] { return m_bindings.setBoxSize(box.c_str(), 2.0f, 3.0f, 4.0f); },
      m_boxObject->getUUID(), m_box },
    { "setSphereOffset", [&] { return m_bindings.setSphereOffset(sphere.c_str(), 1.0f, 2.0f, 3.0f); },
      m_sphereObject->getUUID(), m_sphere },
    { "setSphereRadius", [&] { return m_bindings.setSphereRadius(sphere.c_str(), 2.5f); },
      m_sphereObject->getUUID(), m_sphere },
    { "setCapsuleOffset", [&] { return m_bindings.setCapsuleOffset(capsule.c_str(), 1.0f, 2.0f, 3.0f); },
      m_capsuleObject->getUUID(), m_capsule },
    { "setCapsuleRadius", [&] { return m_bindings.setCapsuleRadius(capsule.c_str(), 0.75f); },
      m_capsuleObject->getUUID(), m_capsule },
    { "setCapsuleHeight", [&] { return m_bindings.setCapsuleHeight(capsule.c_str(), 3.0f); },
      m_capsuleObject->getUUID(), m_capsule },
    { "setConvexMeshOffset", [&] { return m_bindings.setConvexMeshOffset(hullUUID.c_str(), 1.0f, 2.0f, 3.0f); },
      hullObject->getUUID(), hull }
  };

  ASSERT_TRUE(BindingContext::takeComponentEdits().empty());
  for (const auto& entry : cases)
  {
    SCOPED_TRACE(entry.name);

    EXPECT_TRUE(entry.set());

    const auto edits = BindingContext::takeComponentEdits();
    EXPECT_EQ(edits.size(), 1u);
    if (edits.size() == 1u)
    {
      EXPECT_EQ(edits[0].first, entry.object);
      EXPECT_EQ(edits[0].second, entry.component);
    }
  }
}

TEST_F(ColliderBindingsTest, ALayerClampedToItsAlreadyStoredValueRecordsNoEditButADifferentClampDoes)
{
  const auto boxUUID = uuidOf(m_boxObject);

  // Positive control: the first clamp changes the stored layer from 0 to 31, so it records.
  EXPECT_TRUE(m_bindings.setLayer(boxUUID.c_str(), 999u));
  EXPECT_EQ(m_bindings.getLayer(boxUUID.c_str()), 31u);
  EXPECT_EQ(BindingContext::takeComponentEdits().size(), 1u);

  // The same out-of-range request now clamps to the value already stored (31), so it records nothing.
  EXPECT_TRUE(m_bindings.setLayer(boxUUID.c_str(), 999u));
  EXPECT_EQ(m_bindings.getLayer(boxUUID.c_str()), 31u);
  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());

  // A request that resolves to a different value than the one stored records again.
  EXPECT_TRUE(m_bindings.setLayer(boxUUID.c_str(), 7u));
  EXPECT_EQ(BindingContext::takeComponentEdits().size(), 1u);
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

TEST_F(ColliderBindingsTest, CapsuleAccessorsRoundTripAndIgnoreNonFiniteInput)
{
  const auto capsuleUUID = uuidOf(m_capsuleObject);

  float x = 0, y = 0, z = 0, radius = 0, height = 0;
  ASSERT_TRUE(m_bindings.getCapsuleOffset(capsuleUUID.c_str(), &x, &y, &z));
  EXPECT_EQ(glm::vec3(x, y, z), glm::vec3(0.0f));
  ASSERT_TRUE(m_bindings.getCapsuleRadius(capsuleUUID.c_str(), &radius));
  EXPECT_EQ(radius, 0.5f);
  ASSERT_TRUE(m_bindings.getCapsuleHeight(capsuleUUID.c_str(), &height));
  EXPECT_EQ(height, 2.0f);

  EXPECT_TRUE(m_bindings.setCapsuleOffset(capsuleUUID.c_str(), 1.0f, 2.0f, 3.0f));
  EXPECT_TRUE(m_bindings.setCapsuleRadius(capsuleUUID.c_str(), 0.75f));
  EXPECT_TRUE(m_bindings.setCapsuleHeight(capsuleUUID.c_str(), 3.0f));
  EXPECT_EQ(m_capsule->getLocalPosition(), glm::vec3(1.0f, 2.0f, 3.0f));
  EXPECT_EQ(m_capsule->getLocalRadius(), 0.75f);
  EXPECT_EQ(m_capsule->getLocalHeight(), 3.0f);

  const auto nan = std::numeric_limits<float>::quiet_NaN();
  EXPECT_TRUE(m_bindings.setCapsuleOffset(capsuleUUID.c_str(), nan, 0.0f, 0.0f));
  EXPECT_TRUE(m_bindings.setCapsuleRadius(capsuleUUID.c_str(), nan));
  EXPECT_TRUE(m_bindings.setCapsuleHeight(capsuleUUID.c_str(), nan));
  EXPECT_EQ(m_capsule->getLocalPosition(), glm::vec3(1.0f, 2.0f, 3.0f));
  EXPECT_EQ(m_capsule->getLocalRadius(), 0.75f);
  EXPECT_EQ(m_capsule->getLocalHeight(), 3.0f);
}

TEST_F(ColliderBindingsTest, CapsuleAccessorsFailSafelyOnOtherShapes)
{
  const auto boxUUID = uuidOf(m_boxObject);
  const auto bareUUID = uuidOf(m_bareObject);

  float x = 7, y = 7, z = 7, value = 7;
  for (const auto& uuid : { boxUUID, bareUUID })
  {
    EXPECT_FALSE(m_bindings.getCapsuleOffset(uuid.c_str(), &x, &y, &z));
    EXPECT_FALSE(m_bindings.setCapsuleOffset(uuid.c_str(), 1.0f, 1.0f, 1.0f));
    EXPECT_FALSE(m_bindings.getCapsuleRadius(uuid.c_str(), &value));
    EXPECT_FALSE(m_bindings.setCapsuleRadius(uuid.c_str(), 9.0f));
    EXPECT_FALSE(m_bindings.getCapsuleHeight(uuid.c_str(), &value));
    EXPECT_FALSE(m_bindings.setCapsuleHeight(uuid.c_str(), 9.0f));
  }

  EXPECT_EQ(glm::vec3(x, y, z), glm::vec3(7.0f));
  EXPECT_EQ(value, 7.0f);
  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());

  // Positive control: the same accessors succeed on the capsule, and the box-only ones fail on it.
  const auto capsuleUUID = uuidOf(m_capsuleObject);
  EXPECT_TRUE(m_bindings.getCapsuleRadius(capsuleUUID.c_str(), &value));
  EXPECT_FALSE(m_bindings.getBoxSize(capsuleUUID.c_str(), &x, &y, &z));
  EXPECT_FALSE(m_bindings.getSphereRadius(capsuleUUID.c_str(), &value));
}

TEST_F(ColliderBindingsTest, ACapsuleSetRecordsAnEditOnlyWhenTheValueChanged)
{
  const auto capsuleUUID = uuidOf(m_capsuleObject);
  const auto nan = std::numeric_limits<float>::quiet_NaN();

  EXPECT_TRUE(m_bindings.setCapsuleRadius(capsuleUUID.c_str(), nan));
  EXPECT_TRUE(m_bindings.setCapsuleHeight(capsuleUUID.c_str(), 2.0f));
  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());

  EXPECT_TRUE(m_bindings.setCapsuleHeight(capsuleUUID.c_str(), 4.0f));

  const auto edits = BindingContext::takeComponentEdits();
  ASSERT_EQ(edits.size(), 1u);
  EXPECT_EQ(edits[0].first, m_capsuleObject->getUUID());
  EXPECT_EQ(edits[0].second, m_capsule);
}

TEST_F(ColliderBindingsTest, GetShapeNamesAConvexMeshAndItsAccessorsReadAndWriteTheOffset)
{
  const auto hullObject = fixtures::addObject(m_scene, "Hull");
  const auto hull = std::make_shared<ConvexMeshCollider>();
  hullObject->addComponent(hull);
  const auto hullUUID = uuidOf(hullObject);

  EXPECT_EQ(m_bindings.getShape(hullUUID.c_str()), 4);
  EXPECT_TRUE(m_bindings.has(hullUUID.c_str()));

  float x = -1, y = -1, z = -1;
  ASSERT_TRUE(m_bindings.getConvexMeshOffset(hullUUID.c_str(), &x, &y, &z));
  EXPECT_EQ(glm::vec3(x, y, z), glm::vec3(0.0f));

  EXPECT_TRUE(m_bindings.setConvexMeshOffset(hullUUID.c_str(), 1.0f, 2.0f, 3.0f));
  ASSERT_TRUE(m_bindings.getConvexMeshOffset(hullUUID.c_str(), &x, &y, &z));
  EXPECT_EQ(glm::vec3(x, y, z), glm::vec3(1.0f, 2.0f, 3.0f));
  EXPECT_EQ(hull->getLocalPosition(), glm::vec3(1.0f, 2.0f, 3.0f));

  const auto edits = BindingContext::takeComponentEdits();
  ASSERT_EQ(edits.size(), 1u);
  EXPECT_EQ(edits[0].first, hullObject->getUUID());
  EXPECT_EQ(edits[0].second, hull);

  // A non-finite offset is accepted but changes nothing, so it records nothing.
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  EXPECT_TRUE(m_bindings.setConvexMeshOffset(hullUUID.c_str(), nan, 0.0f, 0.0f));
  EXPECT_EQ(hull->getLocalPosition(), glm::vec3(1.0f, 2.0f, 3.0f));
  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
}

TEST_F(ColliderBindingsTest, AConvexMeshReportsItsVertexCountAndTheCountFollowsTheHull)
{
  const auto hullObject = fixtures::addObject(m_scene, "Hull");
  const auto hull = std::make_shared<ConvexMeshCollider>();
  hullObject->addComponent(hull);
  const auto hullUUID = uuidOf(hullObject);

  uint32_t count = 0;
  ASSERT_TRUE(m_bindings.getConvexMeshVertexCount(hullUUID.c_str(), &count));
  EXPECT_EQ(count, 8u);

  const std::vector<glm::vec3> points{ { 0, 0, 0 }, { 2, 0, 0 }, { 0, 2, 0 }, { 0, 0, 2 } };
  const auto tetrahedron = buildConvexHull(points);
  ASSERT_TRUE(tetrahedron.has_value());
  ASSERT_TRUE(hull->setHull(*tetrahedron));

  ASSERT_TRUE(m_bindings.getConvexMeshVertexCount(hullUUID.c_str(), &count));
  EXPECT_EQ(count, 4u);
}

TEST_F(ColliderBindingsTest, ConvexMeshAccessorsFailSafelyOnOtherShapesAndTheOtherWayRound)
{
  const auto hullObject = fixtures::addObject(m_scene, "Hull");
  hullObject->addComponent(std::make_shared<ConvexMeshCollider>());
  const auto hullUUID = uuidOf(hullObject);

  float x = -7, y = -7, z = -7;
  uint32_t count = 99;
  for (const auto& uuid : { uuidOf(m_boxObject), uuidOf(m_capsuleObject), uuidOf(m_bareObject) })
  {
    EXPECT_FALSE(m_bindings.getConvexMeshOffset(uuid.c_str(), &x, &y, &z));
    EXPECT_FALSE(m_bindings.setConvexMeshOffset(uuid.c_str(), 1.0f, 1.0f, 1.0f));
    EXPECT_FALSE(m_bindings.getConvexMeshVertexCount(uuid.c_str(), &count));
  }
  EXPECT_EQ(glm::vec3(x, y, z), glm::vec3(-7.0f));
  EXPECT_EQ(count, 99u);
  EXPECT_EQ(m_box->getLocalPosition(), glm::vec3(0));
  EXPECT_EQ(m_capsule->getLocalPosition(), glm::vec3(0));
  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());

  // The other shapes' accessors refuse a convex mesh, with its state untouched.
  float value = -7;
  EXPECT_FALSE(m_bindings.getBoxOffset(hullUUID.c_str(), &x, &y, &z));
  EXPECT_FALSE(m_bindings.setBoxSize(hullUUID.c_str(), 5.0f, 5.0f, 5.0f));
  EXPECT_FALSE(m_bindings.getSphereRadius(hullUUID.c_str(), &value));
  EXPECT_FALSE(m_bindings.getCapsuleRadius(hullUUID.c_str(), &value));
  EXPECT_FALSE(m_bindings.setCapsuleOffset(hullUUID.c_str(), 5.0f, 5.0f, 5.0f));
  EXPECT_EQ(value, -7.0f);
  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());

  // Positive control: the convex accessors work on the hull.
  EXPECT_TRUE(m_bindings.getConvexMeshOffset(hullUUID.c_str(), &x, &y, &z));
  EXPECT_TRUE(m_bindings.getConvexMeshVertexCount(hullUUID.c_str(), &count));
}
