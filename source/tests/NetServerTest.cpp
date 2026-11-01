#include <gtest/gtest.h>

#include "FakeTransports.h"
#include <NetServer.h>
#include <Protocol.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
  constexpr auto editorRole = static_cast<uint8_t>(net::Role::editor);
  constexpr auto playerRole = static_cast<uint8_t>(net::Role::player);
  constexpr auto snapshotType = static_cast<uint8_t>(net::MessageType::snapshot);
  constexpr auto deltaType = static_cast<uint8_t>(net::MessageType::stateDelta);
  constexpr int32_t boundPort = 41234;

  std::vector<int32_t> sorted(std::vector<int32_t> ids)
  {
    std::ranges::sort(ids);
    return ids;
  }

  std::vector<uint8_t> bytesOf(const net::Message& message)
  {
    return {message.bytes().begin(), message.bytes().end()};
  }

  net::Message messageWith(const net::MessageType type, const std::vector<uint8_t>& payload)
  {
    return {type, payload};
  }
}

class NetServerTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    transport = std::make_shared<FakeServerTransport>();
    transport->startResult = boundPort;
    server = std::make_shared<net::NetServer>(transport);
  }

  // The active-server pointer is process-wide, so no test may leave a started server behind.
  void TearDown() override
  {
    server->stop();
  }

  void receive(const int32_t connId, const uint8_t type, const std::vector<uint8_t>& payload) const
  {
    transport->callbacks.receive(connId, type, payload.data(), static_cast<int32_t>(payload.size()));
  }

  std::shared_ptr<FakeServerTransport> transport;
  std::shared_ptr<net::NetServer> server;
};

TEST_F(NetServerTest, StartForwardsPortEditModeAndTokenAndReportsTheBoundPort)
{
  server->start(0, true, "secret");

  ASSERT_EQ(transport->startCalls.size(), 1u);
  EXPECT_EQ(transport->startCalls[0].port, 0);
  EXPECT_TRUE(transport->startCalls[0].editMode);
  EXPECT_EQ(transport->startCalls[0].authToken, "secret");
  EXPECT_EQ(server->port(), boundPort);
}

TEST_F(NetServerTest, StartForwardsAPlayModeServer)
{
  server->start(3000, false, "");

  ASSERT_EQ(transport->startCalls.size(), 1u);
  EXPECT_EQ(transport->startCalls[0].port, 3000);
  EXPECT_FALSE(transport->startCalls[0].editMode);
  EXPECT_TRUE(transport->startCalls[0].authToken.empty());
}

TEST_F(NetServerTest, AFailedStartThrowsAndLeavesTheServerStopped)
{
  struct Case
  {
    net::ServerStartError error;
    std::string expected;
  };

  const std::vector<Case> cases = {
    {net::ServerStartError::addressInUse, "already in use"},
    {net::ServerStartError::invalidPort, "out of range"},
    {net::ServerStartError::failed, "Could not listen"},
  };

  for (const auto& [error, expected] : cases)
  {
    transport->startResult = static_cast<int32_t>(error);

    try
    {
      server->start(3000, false, "");
      ADD_FAILURE() << "start did not throw for " << expected;
    }
    catch (const std::runtime_error& thrown)
    {
      EXPECT_NE(std::string(thrown.what()).find(expected), std::string::npos) << thrown.what();
      EXPECT_NE(std::string(thrown.what()).find("3000"), std::string::npos) << thrown.what();
    }

    EXPECT_EQ(server->port(), 0);

    server->broadcast(messageWith(net::MessageType::snapshot, {1}));
    EXPECT_TRUE(transport->broadcastCalls.empty());

    receive(1, snapshotType, {1});
    net::Message message;
    int32_t sender = 0;
    EXPECT_FALSE(server->poll(message, sender));
  }

  EXPECT_EQ(transport->startCalls.size(), cases.size());
}

TEST_F(NetServerTest, ACallbackReachesAStartedServer)
{
  server->start(0, false, "");
  receive(7, snapshotType, {1});

  net::Message message;
  int32_t sender = 0;
  EXPECT_TRUE(server->poll(message, sender));
  EXPECT_EQ(sender, 7);
}

TEST_F(NetServerTest, AFailedStartCanBeRetried)
{
  transport->startResult = static_cast<int32_t>(net::ServerStartError::addressInUse);
  EXPECT_THROW(server->start(3000, false, ""), std::runtime_error);

  transport->startResult = boundPort;
  server->start(3000, false, "");

  EXPECT_EQ(server->port(), boundPort);
  EXPECT_EQ(transport->startCalls.size(), 2u);

  receive(7, snapshotType, {1});
  net::Message message;
  int32_t sender = 0;
  EXPECT_TRUE(server->poll(message, sender));
}

TEST_F(NetServerTest, ASecondStartWhileStartedDoesNotCallTheTransportAgain)
{
  server->start(0, false, "");
  server->start(1234, true, "other");

  ASSERT_EQ(transport->startCalls.size(), 1u);
  EXPECT_EQ(transport->startCalls[0].port, 0);
  EXPECT_EQ(server->port(), boundPort);
}

TEST_F(NetServerTest, SendsAreNoOpsBeforeStart)
{
  transport->connectionCountResult = 5;

  server->broadcast(messageWith(net::MessageType::snapshot, {1}));
  server->sendTo(3, messageWith(net::MessageType::snapshot, {1}));
  server->authorize(4, editorRole);
  server->sendToEditors(messageWith(net::MessageType::serverLog, {1}));

  EXPECT_TRUE(transport->broadcastCalls.empty());
  EXPECT_TRUE(transport->sendToManyCalls.empty());
  EXPECT_EQ(server->connectionCount(), 0);
  EXPECT_EQ(transport->connectionCountCalls, 0);

  server->stop();
  EXPECT_EQ(transport->stopCalls, 0);

  server->start(0, true, "");
  server->broadcast(messageWith(net::MessageType::snapshot, {1}));
  server->sendTo(3, messageWith(net::MessageType::snapshot, {1}));
  server->sendToEditors(messageWith(net::MessageType::serverLog, {1}));

  EXPECT_EQ(transport->broadcastCalls.size(), 1u);
  EXPECT_EQ(transport->sendToManyCalls.size(), 2u);
  EXPECT_EQ(server->connectionCount(), 5);
}

TEST_F(NetServerTest, BroadcastForwardsTheTypeAndExactBytes)
{
  server->start(0, false, "");

  server->broadcast(messageWith(net::MessageType::stateDelta, {9, 0, 255, 4}));

  ASSERT_EQ(transport->broadcastCalls.size(), 1u);
  EXPECT_EQ(transport->broadcastCalls[0].type, deltaType);
  EXPECT_EQ(transport->broadcastCalls[0].payload, (std::vector<uint8_t>{9, 0, 255, 4}));
  EXPECT_TRUE(transport->sendToManyCalls.empty());
}

TEST_F(NetServerTest, BroadcastOfAnEmptyMessageStillReachesTheTransport)
{
  server->start(0, false, "");

  server->broadcast(net::Message(net::MessageType::editStatus));

  ASSERT_EQ(transport->broadcastCalls.size(), 1u);
  EXPECT_TRUE(transport->broadcastCalls[0].payload.empty());
}

TEST_F(NetServerTest, SendToEditorsMakesOneCallNamingOnlyTheEditors)
{
  server->start(0, true, "");
  transport->callbacks.authorized(1, editorRole);
  transport->callbacks.authorized(2, playerRole);
  transport->callbacks.authorized(3, editorRole);

  server->sendToEditors(messageWith(net::MessageType::serverLog, {5, 6}));

  ASSERT_EQ(transport->sendToManyCalls.size(), 1u);
  EXPECT_EQ(sorted(transport->sendToManyCalls[0].connIds), (std::vector<int32_t>{1, 3}));
  EXPECT_EQ(transport->sendToManyCalls[0].type, static_cast<uint8_t>(net::MessageType::serverLog));
  EXPECT_EQ(transport->sendToManyCalls[0].payload, (std::vector<uint8_t>{5, 6}));
  EXPECT_TRUE(transport->broadcastCalls.empty());
}

TEST_F(NetServerTest, SendToEditorsWithNoEditorsMakesNoCall)
{
  server->start(0, true, "");
  transport->callbacks.authorized(2, playerRole);

  server->sendToEditors(messageWith(net::MessageType::serverLog, {1}));
  EXPECT_TRUE(transport->sendToManyCalls.empty());

  transport->callbacks.authorized(4, editorRole);
  server->sendToEditors(messageWith(net::MessageType::serverLog, {1}));
  EXPECT_EQ(transport->sendToManyCalls.size(), 1u);
}

TEST_F(NetServerTest, ARevokedEditorIsDroppedFromTheFanOut)
{
  server->start(0, true, "");
  transport->callbacks.authorized(1, editorRole);
  transport->callbacks.authorized(3, editorRole);

  server->revokeEditor(1);
  server->sendToEditors(messageWith(net::MessageType::serverLog, {1}));

  ASSERT_EQ(transport->sendToManyCalls.size(), 1u);
  EXPECT_EQ(transport->sendToManyCalls[0].connIds, (std::vector<int32_t>{3}));
}

TEST_F(NetServerTest, SendToNamesOnlyThatConnection)
{
  server->start(0, false, "");

  server->sendTo(9, messageWith(net::MessageType::snapshot, {7}));

  ASSERT_EQ(transport->sendToManyCalls.size(), 1u);
  EXPECT_EQ(transport->sendToManyCalls[0].connIds, (std::vector<int32_t>{9}));
  EXPECT_EQ(transport->sendToManyCalls[0].type, snapshotType);
  EXPECT_EQ(transport->sendToManyCalls[0].payload, (std::vector<uint8_t>{7}));
  EXPECT_TRUE(transport->broadcastCalls.empty());
}

TEST_F(NetServerTest, PollReturnsMessagesInArrivalOrderWithTheirSenders)
{
  server->start(0, false, "");

  receive(1, snapshotType, {1, 2});
  receive(2, deltaType, {3});
  receive(1, deltaType, {4, 5, 6});

  net::Message message;
  int32_t sender = 0;

  ASSERT_TRUE(server->poll(message, sender));
  EXPECT_EQ(sender, 1);
  EXPECT_EQ(message.getType(), net::MessageType::snapshot);
  EXPECT_EQ(bytesOf(message), (std::vector<uint8_t>{1, 2}));

  ASSERT_TRUE(server->poll(message, sender));
  EXPECT_EQ(sender, 2);
  EXPECT_EQ(message.getType(), net::MessageType::stateDelta);
  EXPECT_EQ(bytesOf(message), (std::vector<uint8_t>{3}));

  ASSERT_TRUE(server->poll(message, sender));
  EXPECT_EQ(sender, 1);
  EXPECT_EQ(bytesOf(message), (std::vector<uint8_t>{4, 5, 6}));

  EXPECT_FALSE(server->poll(message, sender));
}

TEST_F(NetServerTest, ANullBufferOrNegativeLengthArrivesAsAnEmptyPayload)
{
  server->start(0, false, "");
  const uint8_t bytes[] = {1, 2, 3};

  transport->callbacks.receive(1, snapshotType, nullptr, 3);
  transport->callbacks.receive(2, deltaType, bytes, -1);
  transport->callbacks.receive(3, deltaType, bytes, 0);
  transport->callbacks.receive(4, deltaType, bytes, 3);

  net::Message message;
  int32_t sender = 0;

  for (const int32_t expectedSender : {1, 2, 3})
  {
    ASSERT_TRUE(server->poll(message, sender));
    EXPECT_EQ(sender, expectedSender);
    EXPECT_TRUE(message.bytes().empty());
  }

  ASSERT_TRUE(server->poll(message, sender));
  EXPECT_EQ(sender, 4);
  EXPECT_EQ(bytesOf(message), (std::vector<uint8_t>{1, 2, 3}));
}

TEST_F(NetServerTest, TakeDisconnectedReturnsTheDroppedIdsInOrderOnce)
{
  server->start(0, false, "");

  transport->callbacks.disconnect(5);
  transport->callbacks.disconnect(2);

  EXPECT_EQ(server->takeDisconnected(), (std::vector<int32_t>{5, 2}));
  EXPECT_TRUE(server->takeDisconnected().empty());
}

TEST_F(NetServerTest, ADroppedEditorStaysAnEditorUntilRevoked)
{
  server->start(0, true, "");
  transport->callbacks.authorized(4, editorRole);
  transport->callbacks.authorized(5, playerRole);
  EXPECT_TRUE(server->isEditor(4));
  EXPECT_FALSE(server->isEditor(5));

  transport->callbacks.disconnect(4);
  EXPECT_EQ(server->takeDisconnected(), (std::vector<int32_t>{4}));
  EXPECT_TRUE(server->isEditor(4));

  server->revokeEditor(4);
  EXPECT_FALSE(server->isEditor(4));
}

TEST_F(NetServerTest, StopCallsTheTransportOnceAndDetachesTheCallbacks)
{
  server->start(0, true, "");
  transport->callbacks.authorized(1, editorRole);
  const auto callbacks = transport->callbacks;

  server->stop();

  EXPECT_EQ(transport->stopCalls, 1);
  EXPECT_EQ(server->port(), 0);

  callbacks.receive(1, snapshotType, nullptr, 0);
  callbacks.disconnect(1);
  callbacks.authorized(8, editorRole);

  net::Message message;
  int32_t sender = 0;
  EXPECT_FALSE(server->poll(message, sender));
  EXPECT_TRUE(server->takeDisconnected().empty());
  EXPECT_FALSE(server->isEditor(8));

  server->broadcast(messageWith(net::MessageType::snapshot, {1}));
  server->sendTo(1, messageWith(net::MessageType::snapshot, {1}));
  server->sendToEditors(messageWith(net::MessageType::serverLog, {1}));
  EXPECT_TRUE(transport->broadcastCalls.empty());
  EXPECT_TRUE(transport->sendToManyCalls.empty());

  server->stop();
  EXPECT_EQ(transport->stopCalls, 1);
}

TEST_F(NetServerTest, AStoppedServerCanBeStartedAgain)
{
  server->start(0, false, "");
  server->stop();

  server->start(0, false, "");
  receive(2, snapshotType, {1});

  net::Message message;
  int32_t sender = 0;
  EXPECT_TRUE(server->poll(message, sender));
  EXPECT_EQ(transport->startCalls.size(), 2u);
}

TEST_F(NetServerTest, ConnectionCountForwardsTheTransportsCountWhileStarted)
{
  transport->connectionCountResult = 3;
  server->start(0, false, "");

  EXPECT_EQ(server->connectionCount(), 3);

  transport->connectionCountResult = 0;
  EXPECT_EQ(server->connectionCount(), 0);
  EXPECT_EQ(transport->connectionCountCalls, 2);
}
