#include <gtest/gtest.h>

#include "ObjectManagerFixtures.h"
#include "TestScene.h"

#include "BindingContext.h"
#include "InputState.h"
#include "InputUtilsBindings.h"

#include "objects/Object.h"
#include "objects/components/PlayerController.h"

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <uuid.h>

namespace {
  constexpr int32_t slotA = 51;
  constexpr int32_t slotB = 52;
  constexpr int keyW = 87;
  constexpr int keyS = 83;

  std::string uuidOf(const std::shared_ptr<Object>& object)
  {
    return uuids::to_string(object->getUUID());
  }

  class InputUtilsBindingsTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
      scene = fixtures::makeScene();
      BindingContext::setObjectManager(scene.objectManager.get());
      bindings = InputUtilsBindingsProvider::getBindings();

      playerA = addPlayer("playerA", slotA);
      playerB = addPlayer("playerB", slotB);
      uuidA = uuidOf(playerA);
      uuidB = uuidOf(playerB);
    }

    void TearDown() override
    {
      BindingContext::setObjectManager(nullptr);
      InputState::removeSlot(slotA);
      InputState::removeSlot(slotB);
      InputState::removeSlot(0);
    }

    std::shared_ptr<Object> addPlayer(const std::string& name, const int32_t slot)
    {
      const auto object = fixtures::addObject(scene, name);
      const auto controller = std::make_shared<PlayerController>();
      object->addComponent(controller);
      controller->setPlayerSlot(slot);
      return object;
    }

    fixtures::Scene scene;
    InputUtilsBindings bindings{};
    std::shared_ptr<Object> playerA;
    std::shared_ptr<Object> playerB;
    std::string uuidA;
    std::string uuidB;
  };

  TEST_F(InputUtilsBindingsTest, EachObjectReadsItsOwnPlayersKeysFocusAndEdges)
  {
    InputState::setKeysPressed(slotA, { keyW });
    InputState::setFocused(slotA, true);
    InputState::setKeysPressed(slotB, { keyS });

    EXPECT_TRUE(bindings.keyIsPressedForObject(uuidA.c_str(), keyW));
    EXPECT_FALSE(bindings.keyIsPressedForObject(uuidA.c_str(), keyS));
    EXPECT_TRUE(bindings.keyIsPressedForObject(uuidB.c_str(), keyS));
    EXPECT_FALSE(bindings.keyIsPressedForObject(uuidB.c_str(), keyW));

    EXPECT_TRUE(bindings.windowIsFocusedForObject(uuidA.c_str()));
    EXPECT_FALSE(bindings.windowIsFocusedForObject(uuidB.c_str()));

    EXPECT_TRUE(bindings.wasKeyPressedThisTickForObject(uuidA.c_str(), keyW));
    EXPECT_FALSE(bindings.wasKeyPressedThisTickForObject(uuidB.c_str(), keyW));
    EXPECT_FALSE(bindings.wasKeyReleasedThisTickForObject(uuidA.c_str(), keyW));

    InputState::commitInputEdges();
    InputState::setKeysPressed(slotA, {});

    EXPECT_FALSE(bindings.wasKeyPressedThisTickForObject(uuidA.c_str(), keyW));
    EXPECT_TRUE(bindings.wasKeyReleasedThisTickForObject(uuidA.c_str(), keyW));
    EXPECT_FALSE(bindings.wasKeyReleasedThisTickForObject(uuidB.c_str(), keyS));
  }

  TEST_F(InputUtilsBindingsTest, EachObjectReadsItsOwnPlayersMouse)
  {
    InputState::setMouse(slotA, 10.0f, 20.0f, 1.5f, -2.0f, 0.5f, 0b001u);
    InputState::setMouse(slotB, 30.0f, 40.0f, 3.0f, 4.0f, 1.0f, 0b100u);

    float x = 0.0f;
    float y = 0.0f;

    bindings.mousePositionForObject(uuidA.c_str(), &x, &y);
    EXPECT_FLOAT_EQ(x, 10.0f);
    EXPECT_FLOAT_EQ(y, 20.0f);
    bindings.mousePositionForObject(uuidB.c_str(), &x, &y);
    EXPECT_FLOAT_EQ(x, 30.0f);
    EXPECT_FLOAT_EQ(y, 40.0f);

    bindings.mouseDeltaForObject(uuidA.c_str(), &x, &y);
    EXPECT_FLOAT_EQ(x, 1.5f);
    EXPECT_FLOAT_EQ(y, -2.0f);
    bindings.mouseDeltaForObject(uuidB.c_str(), &x, &y);
    EXPECT_FLOAT_EQ(x, 3.0f);
    EXPECT_FLOAT_EQ(y, 4.0f);

    EXPECT_FLOAT_EQ(bindings.scrollForObject(uuidA.c_str()), 0.5f);
    EXPECT_FLOAT_EQ(bindings.scrollForObject(uuidB.c_str()), 1.0f);

    EXPECT_TRUE(bindings.mouseButtonForObject(uuidA.c_str(), 0));
    EXPECT_FALSE(bindings.mouseButtonForObject(uuidA.c_str(), 2));
    EXPECT_TRUE(bindings.mouseButtonForObject(uuidB.c_str(), 2));
    EXPECT_FALSE(bindings.mouseButtonForObject(uuidB.c_str(), 0));
  }

  TEST_F(InputUtilsBindingsTest, ObjectsWithoutAPlayerSlotReadNeutralNotSlotZero)
  {
    // A PlayerController's slot defaults to 0, so a lookup that fell back to slot 0 would read this.
    InputState::setKeysPressed(0, { keyW });
    InputState::setFocused(0, true);
    InputState::setMouse(0, 10.0f, 20.0f, 1.5f, -2.0f, 0.5f, 0b001u);

    const auto zeroPlayer = addPlayer("zeroPlayer", 0);
    const auto zeroUuid = uuidOf(zeroPlayer);
    const auto bare = fixtures::addObject(scene, "bare");
    const auto bareUuid = uuidOf(bare);
    const auto unknownUuid = uuids::to_string(objectManagerFixtures::unknownUUID());
    const std::string malformed = "not-a-uuid";

    // Positive control: an object on slot 0 does read that state.
    EXPECT_TRUE(bindings.keyIsPressedForObject(zeroUuid.c_str(), keyW));
    EXPECT_TRUE(bindings.windowIsFocusedForObject(zeroUuid.c_str()));
    EXPECT_TRUE(bindings.mouseButtonForObject(zeroUuid.c_str(), 0));
    EXPECT_FLOAT_EQ(bindings.scrollForObject(zeroUuid.c_str()), 0.5f);

    const char* const badUuids[] = { bareUuid.c_str(), unknownUuid.c_str(), malformed.c_str(), nullptr };
    for (const auto* uuid : badUuids)
    {
      EXPECT_FALSE(bindings.keyIsPressedForObject(uuid, keyW));
      EXPECT_FALSE(bindings.windowIsFocusedForObject(uuid));
      EXPECT_FALSE(bindings.wasKeyPressedThisTickForObject(uuid, keyW));
      EXPECT_FALSE(bindings.wasKeyReleasedThisTickForObject(uuid, keyW));
      EXPECT_FALSE(bindings.mouseButtonForObject(uuid, 0));
      EXPECT_FLOAT_EQ(bindings.scrollForObject(uuid), 0.0f);

      float x = 9.0f;
      float y = 9.0f;
      bindings.mousePositionForObject(uuid, &x, &y);
      EXPECT_FLOAT_EQ(x, 0.0f);
      EXPECT_FLOAT_EQ(y, 0.0f);

      x = 9.0f;
      y = 9.0f;
      bindings.mouseDeltaForObject(uuid, &x, &y);
      EXPECT_FLOAT_EQ(x, 0.0f);
      EXPECT_FLOAT_EQ(y, 0.0f);
    }
  }

  TEST_F(InputUtilsBindingsTest, PerObjectReadsAreNeutralWithoutAnObjectManager)
  {
    InputState::setKeysPressed(slotA, { keyW });
    ASSERT_TRUE(bindings.keyIsPressedForObject(uuidA.c_str(), keyW));

    BindingContext::setObjectManager(nullptr);

    EXPECT_FALSE(bindings.keyIsPressedForObject(uuidA.c_str(), keyW));
  }

  TEST_F(InputUtilsBindingsTest, AggregateBindingsReadEverySlot)
  {
    // Both slots exist, so an aggregate that looked at only one of them would miss the other's state.
    InputState::setKeysPressed(slotA, {});
    InputState::setFocused(slotA, false);
    InputState::setKeysPressed(slotB, {});
    InputState::setFocused(slotB, false);
    EXPECT_FALSE(bindings.keyIsPressed(keyW));
    EXPECT_FALSE(bindings.windowIsFocused());

    for (const auto slot : { slotA, slotB })
    {
      InputState::setKeysPressed(slot, { keyW });
      InputState::setFocused(slot, true);

      EXPECT_TRUE(bindings.keyIsPressed(keyW));
      EXPECT_FALSE(bindings.keyIsPressed(keyS));
      EXPECT_TRUE(bindings.windowIsFocused());

      InputState::setKeysPressed(slot, {});
      InputState::setFocused(slot, false);

      EXPECT_FALSE(bindings.keyIsPressed(keyW));
      EXPECT_FALSE(bindings.windowIsFocused());
    }
  }
}
