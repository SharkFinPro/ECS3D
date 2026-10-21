#include <gtest/gtest.h>

#include "ClientConnectionState.h"

#include <thread>

TEST(ClientConnectionState, StartsDisconnectedWithNothingLost)
{
  net::ClientConnectionState state;

  EXPECT_FALSE(state.isConnected());
  EXPECT_FALSE(state.takeConnectionLost());
}

TEST(ClientConnectionState, SetConnectedIsReadBack)
{
  net::ClientConnectionState state;

  state.setConnected(true);
  EXPECT_TRUE(state.isConnected());

  state.setConnected(false);
  EXPECT_FALSE(state.isConnected());
}

TEST(ClientConnectionState, AnUnrequestedDropClearsConnectedAndReportsLoss)
{
  net::ClientConnectionState state;
  state.beginConnect();
  state.setConnected(true);

  state.onTransportDisconnect();

  EXPECT_FALSE(state.isConnected());
  EXPECT_TRUE(state.takeConnectionLost());
}

TEST(ClientConnectionState, LossIsReportedExactlyOnce)
{
  net::ClientConnectionState state;
  state.setConnected(true);
  state.onTransportDisconnect();

  EXPECT_TRUE(state.takeConnectionLost());
  EXPECT_FALSE(state.takeConnectionLost());
}

TEST(ClientConnectionState, ARequestedDisconnectIsNotALoss)
{
  net::ClientConnectionState state;
  state.beginConnect();
  state.setConnected(true);

  state.requestDisconnect();
  state.onTransportDisconnect();

  EXPECT_FALSE(state.takeConnectionLost());
  EXPECT_TRUE(state.isConnected());
}

TEST(ClientConnectionState, ARequestedDisconnectLeavesAnEarlierLossPending)
{
  net::ClientConnectionState state;
  state.setConnected(true);
  state.onTransportDisconnect();
  state.requestDisconnect();

  EXPECT_TRUE(state.takeConnectionLost());
}

TEST(ClientConnectionState, BeginConnectForgetsAnEarlierRequestedDisconnect)
{
  net::ClientConnectionState state;
  state.requestDisconnect();
  state.onTransportDisconnect();
  EXPECT_FALSE(state.takeConnectionLost());

  state.beginConnect();
  state.setConnected(true);
  state.onTransportDisconnect();

  EXPECT_FALSE(state.isConnected());
  EXPECT_TRUE(state.takeConnectionLost());
}

TEST(ClientConnectionState, UnrequestedDropIsSeenOnTheAppThreadAfterJoin)
{
  // Smoke test for ASan/TSan: the flags are written on a socket thread and read on the app thread.
  net::ClientConnectionState state;
  state.beginConnect();
  state.setConnected(true);

  std::thread socketThread([&state] { state.onTransportDisconnect(); });
  socketThread.join();

  EXPECT_FALSE(state.isConnected());
  EXPECT_TRUE(state.takeConnectionLost());
  EXPECT_FALSE(state.takeConnectionLost());
}
