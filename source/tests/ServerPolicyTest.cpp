#include <gtest/gtest.h>

#include <Protocol.h>
#include <ServerPolicy.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

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

TEST(ServerPolicyTest, PlanJoinAnswersAFirstJoinRegardlessOfWarnedFlag)
{
  EXPECT_EQ(planJoin(false, false), (JoinPlan{true, false}));
  EXPECT_EQ(planJoin(false, true), (JoinPlan{true, false}));
}

TEST(ServerPolicyTest, PlanJoinDropsARepeatAndWarnsOnlyOnce)
{
  EXPECT_EQ(planJoin(true, false), (JoinPlan{false, true}));
  EXPECT_EQ(planJoin(true, true), (JoinPlan{false, false}));
}

// ServerApp cannot be linked into the suite, so the two per-connection sets are stood in for by bools updated
// exactly as ServerApp::handleJoin updates them.
TEST(ServerPolicyTest, PlanJoinSequenceAnswersFirstWarnsSecondAndSilencesThird)
{
  bool joined = false;
  bool warned = false;
  std::vector<JoinPlan> seen;

  for (int i = 0; i < 3; ++i)
  {
    const auto joinPlan = planJoin(joined, warned);
    seen.push_back(joinPlan);

    if (joinPlan.warnRepeat)
    {
      warned = true;
    }

    if (joinPlan.answer)
    {
      joined = true;
    }
  }

  EXPECT_EQ(seen[0], (JoinPlan{true, false}));
  EXPECT_EQ(seen[1], (JoinPlan{false, true}));
  EXPECT_EQ(seen[2], (JoinPlan{false, false}));
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

TEST(ServerPolicyTest, OnlyEditServerForwardsLogToEditors)
{
  static_assert(forwardsLogToEditors(true));
  static_assert(!forwardsLogToEditors(false));
  EXPECT_TRUE(forwardsLogToEditors(true));
  EXPECT_FALSE(forwardsLogToEditors(false));
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
}

TEST(ServerPolicyTest, PauseOnAStoppedSceneIsRefused)
{
  EXPECT_EQ(planSceneControl(net::SceneControlOp::pause, true), plan());
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

TEST(ServerPolicyTest, ScriptsArePolledOnlyForAStoppedSceneOnAnEditServer)
{
  EXPECT_TRUE(shouldPollScriptsWhileStopped(true, true, SceneStatus::stopped));
  EXPECT_FALSE(shouldPollScriptsWhileStopped(true, true, SceneStatus::running));
  EXPECT_FALSE(shouldPollScriptsWhileStopped(true, true, SceneStatus::paused));
  EXPECT_FALSE(shouldPollScriptsWhileStopped(false, true, SceneStatus::stopped));
  EXPECT_FALSE(shouldPollScriptsWhileStopped(true, false, SceneStatus::stopped));
}

TEST(ServerPolicy, SimulatesOnlyWhileAClientIsConnected)
{
  EXPECT_FALSE(shouldSimulate(0, false));
  EXPECT_TRUE(shouldSimulate(1, false));
  EXPECT_TRUE(shouldSimulate(8, false));
}

TEST(ServerPolicy, SimulateWhenEmptyKeepsAnEmptyServerTicking)
{
  EXPECT_TRUE(shouldSimulate(0, true));
  EXPECT_TRUE(shouldSimulate(1, true));
}

TEST(ServerPolicy, DedicatedServerKeepsRunningWithOrWithoutConnections)
{
  EXPECT_TRUE(shouldKeepRunning(false, false, false, 0));
  EXPECT_TRUE(shouldKeepRunning(false, false, true, 0));
  EXPECT_TRUE(shouldKeepRunning(false, false, true, 3));
}

TEST(ServerPolicy, ShutdownRequestStopsEveryServer)
{
  EXPECT_FALSE(shouldKeepRunning(true, false, false, 0));
  EXPECT_FALSE(shouldKeepRunning(true, false, true, 2));
  EXPECT_FALSE(shouldKeepRunning(true, true, false, 0));
  EXPECT_FALSE(shouldKeepRunning(true, true, true, 2));
}

TEST(ServerPolicy, EphemeralServerWaitsForItsFirstConnection)
{
  EXPECT_TRUE(shouldKeepRunning(false, true, false, 0));
}

TEST(ServerPolicy, EphemeralServerStopsAfterItsLastConnectionDrops)
{
  EXPECT_FALSE(shouldKeepRunning(false, true, true, 0));
  EXPECT_TRUE(shouldKeepRunning(false, true, true, 1));
  EXPECT_TRUE(shouldKeepRunning(false, true, true, 4));
}

namespace {
  struct DrainRecorder
  {
    std::vector<std::pair<int, int>> inbox;
    std::vector<int> disconnectQueue;
    std::vector<std::string> events;
    std::function<void(int)> onPoll;
    std::function<void()> onDrained;

    void drain()
    {
      drainInboxThenDisconnects<int>(
        [this] { return std::exchange(disconnectQueue, {}); },
        [this](int& message, int32_t& senderId)
        {
          if (inbox.empty())
          {
            if (onDrained)
            {
              std::exchange(onDrained, {})();
            }
            return false;
          }

          message = inbox.front().second;
          senderId = inbox.front().first;
          inbox.erase(inbox.begin());
          if (onPoll)
          {
            onPoll(message);
          }
          return true;
        },
        [this](const int message, const int32_t senderId)
        {
          events.push_back("message " + std::to_string(senderId) + ":" + std::to_string(message));
        },
        [this](const int32_t connId)
        {
          events.push_back("disconnect " + std::to_string(connId));
        });
    }
  };
}

TEST(ServerPolicy, ADisconnectIsHandledAfterTheMessagesQueuedBeforeIt)
{
  DrainRecorder net;
  net.inbox = { { 5, 1 }, { 6, 2 } };
  net.disconnectQueue = { 5 };

  net.drain();

  EXPECT_EQ(net.events, (std::vector<std::string>{ "message 5:1", "message 6:2", "disconnect 5" }));
}

TEST(ServerPolicy, ADisconnectQueuedDuringTheDrainWaitsForTheNextRoundAfterItsLastMessage)
{
  DrainRecorder net;
  net.inbox = { { 4, 1 } };
  net.onPoll = [&net](const int message)
  {
    if (message == 1)
    {
      net.inbox.push_back({ 5, 2 });
      net.disconnectQueue.push_back(5);
    }
  };

  net.drain();
  EXPECT_EQ(net.events, (std::vector<std::string>{ "message 4:1", "message 5:2" }));

  net.events.clear();
  net.drain();
  EXPECT_EQ(net.events, (std::vector<std::string>{ "disconnect 5" }));
}

TEST(ServerPolicy, ALastMessageAndItsDisconnectArrivingAfterTheDrainAreHandledTogetherNextRound)
{
  DrainRecorder net;
  net.onDrained = [&net]
  {
    net.inbox.push_back({ 5, 9 });
    net.disconnectQueue.push_back(5);
  };

  net.drain();
  EXPECT_TRUE(net.events.empty());

  net.drain();
  EXPECT_EQ(net.events, (std::vector<std::string>{ "message 5:9", "disconnect 5" }));
}
