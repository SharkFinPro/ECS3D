#include <gtest/gtest.h>

#include "TestScene.h"
#include "bindings/BindingContext.h"
#include "bindings/PlayerControllerBindings.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/PlayerController.h"

#include <memory>
#include <string>
#include <uuid.h>

namespace {
  // BindingContext points at whatever ObjectManager the server's current tick is using; each test wires
  // it to its own scene and must undo that (and drop anything it recorded) so it doesn't leak into a test
  // that runs after it - BindingContext's backing state is static, shared across the whole suite.
  class PlayerControllerBindingsTest : public testing::Test {
  protected:
    void SetUp() override
    {
      m_scene = fixtures::makeScene();
      m_object = fixtures::addObject(m_scene, "Player");
      m_playerController = std::make_shared<PlayerController>();
      m_object->addComponent(m_playerController);

      BindingContext::setObjectManager(m_scene.objectManager.get());
      m_bindings = PlayerControllerBindingsProvider::getBindings();
    }

    void TearDown() override
    {
      BindingContext::setObjectManager(nullptr);
      BindingContext::takeComponentEdits();
    }

    fixtures::Scene m_scene;
    std::shared_ptr<Object> m_object;
    std::shared_ptr<PlayerController> m_playerController;
    PlayerControllerBindings m_bindings{};

    [[nodiscard]] std::string uuid() const
    {
      return uuids::to_string(m_object->getUUID());
    }
  };
}

TEST_F(PlayerControllerBindingsTest, HasIsTrueForAnObjectCarryingAPlayerController)
{
  EXPECT_TRUE(m_bindings.has(uuid().c_str()));
}

TEST_F(PlayerControllerBindingsTest, GetReturnsTheSlot)
{
  m_playerController->setPlayerSlot(3);

  EXPECT_EQ(m_bindings.getPlayerSlot(uuid().c_str()), 3);
}

TEST_F(PlayerControllerBindingsTest, SetChangesTheComponentAndRecordsOneEdit)
{
  const auto id = uuid();
  ASSERT_EQ(m_playerController->getPlayerSlot(), 0);

  m_bindings.setPlayerSlot(id.c_str(), 2);

  EXPECT_EQ(m_playerController->getPlayerSlot(), 2);

  // Positive control for the recording behavior the no-op test below relies on: exactly one edit, for
  // this object, after exactly one set call that actually changed the value.
  const auto edits = BindingContext::takeComponentEdits();
  ASSERT_EQ(edits.size(), 1u);
  EXPECT_EQ(edits[0].first, m_object->getUUID());
  EXPECT_EQ(edits[0].second, m_playerController);
}

TEST_F(PlayerControllerBindingsTest, SetToItsCurrentValueRecordsNothing)
{
  const auto id = uuid();
  const auto current = m_playerController->getPlayerSlot();

  m_bindings.setPlayerSlot(id.c_str(), current);

  // Nothing changed, so there is nothing to replicate.
  EXPECT_EQ(m_playerController->getPlayerSlot(), current);
  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
}

TEST_F(PlayerControllerBindingsTest, UnknownUuidGetReturnsNeutralDefault)
{
  const std::string unknown = "12345678-1234-1234-1234-123456789abc";

  EXPECT_FALSE(m_bindings.has(unknown.c_str()));
  EXPECT_EQ(m_bindings.getPlayerSlot(unknown.c_str()), 0);
}

TEST_F(PlayerControllerBindingsTest, UnknownUuidSetChangesNothingAndRecordsNothing)
{
  const std::string unknown = "12345678-1234-1234-1234-123456789abc";

  m_bindings.setPlayerSlot(unknown.c_str(), 5);

  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());

  // Positive control: the same set call against the real object's uuid does record an edit, so the empty
  // result above reflects the unknown-uuid guard rather than some other break in the pipeline.
  const auto id = uuid();
  m_bindings.setPlayerSlot(id.c_str(), 5);
  EXPECT_EQ(BindingContext::takeComponentEdits().size(), 1u);
}

TEST_F(PlayerControllerBindingsTest, MalformedUuidBehavesLikeUnknown)
{
  const char* malformed = "not-a-uuid";

  EXPECT_FALSE(m_bindings.has(malformed));
  EXPECT_EQ(m_bindings.getPlayerSlot(malformed), 0);

  m_bindings.setPlayerSlot(malformed, 1);
  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
}

TEST_F(PlayerControllerBindingsTest, NullUuidBehavesLikeUnknown)
{
  EXPECT_FALSE(m_bindings.has(nullptr));
  EXPECT_EQ(m_bindings.getPlayerSlot(nullptr), 0);

  m_bindings.setPlayerSlot(nullptr, 1);
  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
}

TEST_F(PlayerControllerBindingsTest, ObjectWithoutAPlayerControllerBehavesLikeUnknown)
{
  auto plain = fixtures::addObject(m_scene, "NoController");
  const auto id = uuids::to_string(plain->getUUID());

  EXPECT_FALSE(m_bindings.has(id.c_str()));
  EXPECT_EQ(m_bindings.getPlayerSlot(id.c_str()), 0);

  m_bindings.setPlayerSlot(id.c_str(), 4);
  EXPECT_TRUE(BindingContext::takeComponentEdits().empty());

  // Positive control: has() is true for the fixture's real player controller, so false above reflects the
  // missing component rather than a broken has() implementation.
  EXPECT_TRUE(m_bindings.has(uuid().c_str()));
}
