#include <gtest/gtest.h>

#include "FakeTransports.h"
#include <NetClient.h>
#include <Protocol.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace {
  constexpr auto snapshotType = static_cast<uint8_t>(net::MessageType::snapshot);
  constexpr auto deltaType = static_cast<uint8_t>(net::MessageType::stateDelta);

  std::vector<uint8_t> bytesOf(const net::Message& message)
  {
    return {message.bytes().begin(), message.bytes().end()};
  }

  net::Message messageWith(const net::MessageType type, const std::vector<uint8_t>& payload)
  {
    return {type, payload};
  }
}

class NetClientTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    transport = std::make_shared<FakeClientTransport>();
    client = std::make_shared<net::NetClient>(transport);
  }

  // The active-client pointer is process-wide, so no test may leave a connected client behind.
  void TearDown() override
  {
    client->disconnect();
  }

  std::shared_ptr<FakeClientTransport> transport;
  std::shared_ptr<net::NetClient> client;
};

TEST_F(NetClientTest, ConnectForwardsHostPortRoleAndToken)
{
  client->connect("example.test", 4321, net::Role::editor, "token");

  ASSERT_EQ(transport->connectCalls.size(), 1u);
  EXPECT_EQ(transport->connectCalls[0].host, "example.test");
  EXPECT_EQ(transport->connectCalls[0].port, 4321);
  EXPECT_EQ(transport->connectCalls[0].role, net::Role::editor);
  EXPECT_EQ(transport->connectCalls[0].authToken, "token");
  EXPECT_TRUE(client->isConnected());
}

TEST_F(NetClientTest, AFailedConnectLeavesTheClientDisconnectedAndSendsNothing)
{
  transport->connectResult = false;

  client->connect("localhost", 3000, net::Role::player, "");

  EXPECT_FALSE(client->isConnected());
  client->send(messageWith(net::MessageType::join, {1}));
  EXPECT_TRUE(transport->sendCalls.empty());

  client->disconnect();
  EXPECT_EQ(transport->disconnectCalls, 0);
}

TEST_F(NetClientTest, AThrowingTransportLeavesTheClientDisconnectedAndUnreachable)
{
  transport->throwOnConnect = true;
  EXPECT_THROW(client->connect("localhost", 3000, net::Role::player, ""), std::runtime_error);

  EXPECT_FALSE(client->isConnected());
  const uint8_t bytes[] = {1};
  transport->callbacks.receive(snapshotType, bytes, 1);
  net::Message message;
  EXPECT_FALSE(client->poll(message));

  transport->throwOnConnect = false;
  client->connect("localhost", 3000, net::Role::player, "");
  transport->callbacks.receive(snapshotType, bytes, 1);
  EXPECT_TRUE(client->poll(message));
}

TEST_F(NetClientTest, AFailedConnectCanBeRetried)
{
  transport->connectResult = false;
  client->connect("localhost", 3000, net::Role::player, "");

  transport->connectResult = true;
  client->connect("localhost", 3000, net::Role::player, "");

  EXPECT_TRUE(client->isConnected());
  EXPECT_EQ(transport->connectCalls.size(), 2u);
}

TEST_F(NetClientTest, ConnectWhileConnectedDoesNotCallTheTransportAgain)
{
  client->connect("localhost", 3000, net::Role::player, "");
  client->connect("elsewhere", 1, net::Role::editor, "x");

  ASSERT_EQ(transport->connectCalls.size(), 1u);
  EXPECT_EQ(transport->connectCalls[0].host, "localhost");
}

TEST_F(NetClientTest, SendForwardsOnlyOnceConnected)
{
  client->send(messageWith(net::MessageType::join, {1}));
  EXPECT_TRUE(transport->sendCalls.empty());

  client->connect("localhost", 3000, net::Role::player, "");
  client->send(messageWith(net::MessageType::inputState, {8, 0, 200}));

  ASSERT_EQ(transport->sendCalls.size(), 1u);
  EXPECT_EQ(transport->sendCalls[0].type, static_cast<uint8_t>(net::MessageType::inputState));
  EXPECT_EQ(transport->sendCalls[0].payload, (std::vector<uint8_t>{8, 0, 200}));
}

TEST_F(NetClientTest, PollReturnsMessagesInArrivalOrder)
{
  client->connect("localhost", 3000, net::Role::player, "");
  const uint8_t first[] = {1, 2};
  const uint8_t second[] = {3};

  transport->callbacks.receive(snapshotType, first, 2);
  transport->callbacks.receive(deltaType, second, 1);

  net::Message message;
  ASSERT_TRUE(client->poll(message));
  EXPECT_EQ(message.getType(), net::MessageType::snapshot);
  EXPECT_EQ(bytesOf(message), (std::vector<uint8_t>{1, 2}));

  ASSERT_TRUE(client->poll(message));
  EXPECT_EQ(message.getType(), net::MessageType::stateDelta);
  EXPECT_EQ(bytesOf(message), (std::vector<uint8_t>{3}));

  EXPECT_FALSE(client->poll(message));
}

TEST_F(NetClientTest, ANullBufferOrNegativeLengthArrivesAsAnEmptyPayload)
{
  client->connect("localhost", 3000, net::Role::player, "");
  const uint8_t bytes[] = {1, 2, 3};

  transport->callbacks.receive(snapshotType, nullptr, 3);
  transport->callbacks.receive(deltaType, bytes, -1);
  transport->callbacks.receive(deltaType, bytes, 3);

  net::Message message;
  ASSERT_TRUE(client->poll(message));
  EXPECT_TRUE(message.bytes().empty());
  ASSERT_TRUE(client->poll(message));
  EXPECT_TRUE(message.bytes().empty());
  ASSERT_TRUE(client->poll(message));
  EXPECT_EQ(bytesOf(message), (std::vector<uint8_t>{1, 2, 3}));
}

TEST_F(NetClientTest, AnUnrequestedTransportDisconnectIsReportedLostExactlyOnce)
{
  client->connect("localhost", 3000, net::Role::player, "");
  EXPECT_FALSE(client->takeConnectionLost());

  transport->callbacks.disconnect();

  EXPECT_FALSE(client->isConnected());
  EXPECT_TRUE(client->takeConnectionLost());
  EXPECT_FALSE(client->takeConnectionLost());
}

TEST_F(NetClientTest, ARequestedDisconnectIsNotALossEvenWhenTheTransportReportsIt)
{
  transport->reportDisconnectInsideDisconnect = true;
  client->connect("localhost", 3000, net::Role::player, "");

  client->disconnect();

  EXPECT_EQ(transport->disconnectCalls, 1);
  EXPECT_FALSE(client->isConnected());
  EXPECT_FALSE(client->takeConnectionLost());
}

TEST_F(NetClientTest, DisconnectCallsTheTransportOnceAndDetachesTheCallbacks)
{
  client->connect("localhost", 3000, net::Role::player, "");
  const auto callbacks = transport->callbacks;

  client->disconnect();
  client->disconnect();

  EXPECT_EQ(transport->disconnectCalls, 1);

  const uint8_t bytes[] = {1};
  callbacks.receive(snapshotType, bytes, 1);
  callbacks.disconnect();

  net::Message message;
  EXPECT_FALSE(client->poll(message));
  EXPECT_FALSE(client->takeConnectionLost());

  client->send(messageWith(net::MessageType::join, {1}));
  EXPECT_TRUE(transport->sendCalls.empty());
}

TEST_F(NetClientTest, ADropAfterAReconnectIsReportedAsLost)
{
  transport->reportDisconnectInsideDisconnect = true;
  client->connect("localhost", 3000, net::Role::player, "");
  client->disconnect();
  EXPECT_FALSE(client->takeConnectionLost());

  client->connect("localhost", 3000, net::Role::player, "");
  EXPECT_TRUE(client->isConnected());
  transport->callbacks.disconnect();

  EXPECT_FALSE(client->isConnected());
  EXPECT_TRUE(client->takeConnectionLost());
  EXPECT_EQ(transport->connectCalls.size(), 2u);
}

TEST_F(NetClientTest, SentMessagesAreCountedOnceConnectedAndNotBefore)
{
  client->send(messageWith(net::MessageType::inputState, {1, 2}));
  EXPECT_EQ(client->traffic().totalOut().messages[static_cast<std::size_t>(net::MessageType::inputState)], 0u);

  client->connect("localhost", 3000, net::Role::player, "");
  client->send(messageWith(net::MessageType::inputState, {1, 2}));

  const auto out = client->traffic().totalOut();
  EXPECT_EQ(out.messages[static_cast<std::size_t>(net::MessageType::inputState)], 1u);
  EXPECT_EQ(out.bytes[static_cast<std::size_t>(net::MessageType::inputState)], 3u);
}

TEST_F(NetClientTest, ReceivedMessagesAreCountedWithTheirTypeByte)
{
  client->connect("localhost", 3000, net::Role::player, "");
  const uint8_t bytes[] = {1, 2, 3};

  transport->callbacks.receive(deltaType, bytes, 3);
  transport->callbacks.receive(deltaType, nullptr, 0);

  const auto in = client->traffic().totalIn();
  EXPECT_EQ(in.messages[static_cast<std::size_t>(net::MessageType::stateDelta)], 2u);
  EXPECT_EQ(in.bytes[static_cast<std::size_t>(net::MessageType::stateDelta)], 5u);
}
