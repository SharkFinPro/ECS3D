#include <gtest/gtest.h>

#include <Protocol.h>
#include <ServerPolicy.h>

#include <cstdint>
#include <limits>
#include <string>

namespace {
  // Exhaustive switch with no default so -Wswitch flags a new MessageType until it is stated here.
  // #lizard forgives
  [[nodiscard]] constexpr bool expectedMutation(const net::MessageType type)
  {
    switch (type)
    {
      case net::MessageType::editComponent:
      case net::MessageType::sceneEdit:
      case net::MessageType::sceneControl:
      case net::MessageType::loadProject:
      case net::MessageType::addAsset:
      case net::MessageType::renameAsset:
      case net::MessageType::removeAsset:
        return true;

      case net::MessageType::undefined:
      case net::MessageType::join:
      case net::MessageType::snapshot:
      case net::MessageType::stateDelta:
      case net::MessageType::inputState:
      case net::MessageType::editStatus:
      case net::MessageType::sceneStatus:
      case net::MessageType::objectSpawned:
      case net::MessageType::objectDestroyed:
      case net::MessageType::playerSlot:
      case net::MessageType::serverLog:
      case net::MessageType::objectComponentsChanged:
        return false;
    }

    return false;
  }

  [[nodiscard]] std::string describe(const net::MessageType type, const bool editMode, const bool isEditor)
  {
    return "type=" + std::to_string(static_cast<int>(type)) + " editMode=" + std::to_string(editMode)
      + " isEditor=" + std::to_string(isEditor);
  }

  [[nodiscard]] SceneControlPlan plan(const bool startScene = false, const bool pauseScene = false,
                                      const bool resetScene = false, const bool startScripts = false,
                                      const bool stopScripts = false, const bool resetCollisions = false)
  {
    return SceneControlPlan{startScene, pauseScene, resetScene, startScripts, stopScripts, resetCollisions};
  }
}

TEST(ServerPolicyTest, MutationSetMatchesIndependentlyStatedExpectation)
{
  int mutations = 0;

  for (int value = 0; value <= std::numeric_limits<uint8_t>::max(); ++value)
  {
    const auto type = static_cast<net::MessageType>(value);
    EXPECT_EQ(net::isMutationMessage(type), expectedMutation(type)) << value;

    if (expectedMutation(type))
    {
      ++mutations;
    }
  }

  EXPECT_EQ(mutations, 7);
}

TEST(ServerPolicyTest, MutationsPassOnlyOnEditServerFromEditorConnection)
{
  for (int value = 0; value <= std::numeric_limits<uint8_t>::max(); ++value)
  {
    const auto type = static_cast<net::MessageType>(value);
    const bool isMutation = expectedMutation(type);

    for (const bool editMode : {false, true})
    {
      for (const bool isEditor : {false, true})
      {
        const bool expected = isMutation ? (editMode && isEditor) : true;
        EXPECT_EQ(isMutationAuthorized(type, editMode, isEditor), expected) << describe(type, editMode, isEditor);
      }
    }
  }
}

TEST(ServerPolicyTest, NamedMutationTypes)
{
  for (const auto type : {net::MessageType::editComponent, net::MessageType::sceneEdit,
                          net::MessageType::sceneControl, net::MessageType::loadProject,
                          net::MessageType::addAsset, net::MessageType::renameAsset,
                          net::MessageType::removeAsset})
  {
    EXPECT_TRUE(isMutationAuthorized(type, true, true)) << static_cast<int>(type);
    EXPECT_FALSE(isMutationAuthorized(type, true, false)) << static_cast<int>(type);
    EXPECT_FALSE(isMutationAuthorized(type, false, true)) << static_cast<int>(type);
    EXPECT_FALSE(isMutationAuthorized(type, false, false)) << static_cast<int>(type);
  }
}

TEST(ServerPolicyTest, NonMutationTypesAlwaysPass)
{
  EXPECT_TRUE(isMutationAuthorized(net::MessageType::join, false, false));
  EXPECT_TRUE(isMutationAuthorized(net::MessageType::inputState, false, false));
  EXPECT_TRUE(isMutationAuthorized(net::MessageType::inputState, true, false));
}

TEST(ServerPolicyTest, StartFromStoppedStartsScriptsAndResetsCollisions)
{
  EXPECT_EQ(planSceneControl(net::SceneControlOp::start, true), plan(true, false, false, true, false, true));
}

TEST(ServerPolicyTest, StartFromPausedResumesWithoutTouchingScriptsOrCollisions)
{
  EXPECT_EQ(planSceneControl(net::SceneControlOp::start, false), plan(true));
}

TEST(ServerPolicyTest, PauseNeverTouchesScriptsOrCollisions)
{
  EXPECT_EQ(planSceneControl(net::SceneControlOp::pause, false), plan(false, true));
  EXPECT_EQ(planSceneControl(net::SceneControlOp::pause, true), plan(false, true));
}

TEST(ServerPolicyTest, StopFromRunningStopsScriptsResetsSceneAndCollisions)
{
  EXPECT_EQ(planSceneControl(net::SceneControlOp::stop, false), plan(false, false, true, false, true, true));
}

TEST(ServerPolicyTest, StopFromStoppedDoesNothing)
{
  EXPECT_EQ(planSceneControl(net::SceneControlOp::stop, true), plan());
}

TEST(ServerPolicyTest, LoadSceneHasNoLifecyclePlan)
{
  EXPECT_EQ(planSceneControl(net::SceneControlOp::loadScene, true), plan());
  EXPECT_EQ(planSceneControl(net::SceneControlOp::loadScene, false), plan());
}
