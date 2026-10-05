#include <gtest/gtest.h>

#include <Protocol.h>
#include <ServerPolicy.h>

#include <cstdint>
#include <string>

namespace {
  // MessageType has no count sentinel; the last enumerator bounds the sweep, so a type appended after it
  // must be added here to be covered.
  constexpr auto lastMessageType = static_cast<uint8_t>(net::MessageType::objectComponentsChanged);

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

TEST(ServerPolicyTest, MutationsPassOnlyOnEditServerFromEditorConnection)
{
  int mutations = 0;
  int others = 0;

  for (int value = 0; value <= lastMessageType; ++value)
  {
    const auto type = static_cast<net::MessageType>(value);
    const bool isMutation = net::isMutationMessage(type);
    isMutation ? ++mutations : ++others;

    for (const bool editMode : {false, true})
    {
      for (const bool isEditor : {false, true})
      {
        const bool expected = !isMutation || (editMode && isEditor);
        EXPECT_EQ(isMutationAuthorized(type, editMode, isEditor), expected) << describe(type, editMode, isEditor);
      }
    }
  }

  EXPECT_EQ(mutations, 7);
  EXPECT_GT(others, 0);
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
