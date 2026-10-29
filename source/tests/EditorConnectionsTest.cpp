#include <gtest/gtest.h>

#include "EditorConnections.h"

#include <Protocol.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace {
  constexpr auto editorRole = static_cast<uint8_t>(net::Role::editor);
  constexpr auto playerRole = static_cast<uint8_t>(net::Role::player);

  std::vector<int32_t> sorted(std::vector<int32_t> ids)
  {
    std::ranges::sort(ids);
    return ids;
  }
}

TEST(EditorConnections, AuthorizingAsEditorMakesTheConnectionAnEditor)
{
  net::EditorConnections connections;

  EXPECT_FALSE(connections.isEditor(1));
  connections.authorize(1, editorRole);
  EXPECT_TRUE(connections.isEditor(1));
  EXPECT_FALSE(connections.isEditor(2));
}

TEST(EditorConnections, APlayerRoleIsNotAnEditor)
{
  net::EditorConnections connections;

  connections.authorize(1, editorRole);
  connections.authorize(2, playerRole);

  EXPECT_TRUE(connections.isEditor(1));
  EXPECT_FALSE(connections.isEditor(2));
  EXPECT_EQ(connections.editorIds(), (std::vector<int32_t>{ 1 }));
}

TEST(EditorConnections, AnUnknownRoleByteIsNotAnEditor)
{
  net::EditorConnections connections;

  connections.authorize(1, 200);
  EXPECT_FALSE(connections.isEditor(1));
  EXPECT_TRUE(connections.editorIds().empty());

  connections.authorize(1, editorRole);
  EXPECT_TRUE(connections.isEditor(1));
}

TEST(EditorConnections, EditorIdsListsEveryEditorOnce)
{
  net::EditorConnections connections;
  EXPECT_TRUE(connections.editorIds().empty());

  connections.authorize(5, editorRole);
  connections.authorize(3, editorRole);
  connections.authorize(5, editorRole);
  connections.authorize(4, playerRole);

  EXPECT_EQ(sorted(connections.editorIds()), (std::vector<int32_t>{ 3, 5 }));
}

TEST(EditorConnections, ASnapshotIsACopyAndDoesNotTrackLaterChanges)
{
  net::EditorConnections connections;
  connections.authorize(1, editorRole);

  const auto snapshot = connections.editorIds();
  connections.authorize(2, editorRole);

  EXPECT_EQ(snapshot, (std::vector<int32_t>{ 1 }));
  EXPECT_EQ(sorted(connections.editorIds()), (std::vector<int32_t>{ 1, 2 }));
}

TEST(EditorConnections, QueuingADisconnectKeepsTheEditorUntilItIsTaken)
{
  net::EditorConnections connections;
  connections.authorize(1, editorRole);

  connections.enqueueDisconnect(1);

  EXPECT_TRUE(connections.isEditor(1));
  EXPECT_EQ(connections.editorIds(), (std::vector<int32_t>{ 1 }));

  EXPECT_EQ(connections.takeDisconnected(), (std::vector<int32_t>{ 1 }));
  EXPECT_TRUE(connections.isEditor(1));

  connections.revokeEditor(1);
  EXPECT_FALSE(connections.isEditor(1));
  EXPECT_TRUE(connections.editorIds().empty());
}

TEST(EditorConnections, TakeDisconnectedReturnsEachIdOnceThenClears)
{
  net::EditorConnections connections;
  connections.enqueueDisconnect(7);
  connections.enqueueDisconnect(3);

  EXPECT_EQ(connections.takeDisconnected(), (std::vector<int32_t>{ 7, 3 }));
  EXPECT_TRUE(connections.takeDisconnected().empty());

  connections.enqueueDisconnect(9);
  EXPECT_EQ(connections.takeDisconnected(), (std::vector<int32_t>{ 9 }));
}

TEST(EditorConnections, OnlyTheDisconnectedEditorsAreRevoked)
{
  net::EditorConnections connections;
  connections.authorize(1, editorRole);
  connections.authorize(2, editorRole);

  connections.enqueueDisconnect(1);
  EXPECT_EQ(connections.takeDisconnected(), (std::vector<int32_t>{ 1 }));
  connections.revokeEditor(1);

  EXPECT_FALSE(connections.isEditor(1));
  EXPECT_TRUE(connections.isEditor(2));
  EXPECT_EQ(connections.editorIds(), (std::vector<int32_t>{ 2 }));
}

TEST(EditorConnections, DisconnectingAnUnknownIdIsHarmless)
{
  net::EditorConnections connections;
  connections.authorize(1, editorRole);

  connections.enqueueDisconnect(99);

  EXPECT_EQ(connections.takeDisconnected(), (std::vector<int32_t>{ 99 }));
  EXPECT_TRUE(connections.isEditor(1));
  EXPECT_FALSE(connections.isEditor(99));
}

TEST(EditorConnections, ALaterAuthorizationIsUnaffectedByAnEarlierDisconnect)
{
  net::EditorConnections connections;
  connections.authorize(1, editorRole);
  connections.enqueueDisconnect(1);
  connections.revokeEditor(connections.takeDisconnected().front());

  connections.authorize(2, editorRole);

  EXPECT_FALSE(connections.isEditor(1));
  EXPECT_TRUE(connections.isEditor(2));
}

TEST(EditorConnections, ConcurrentSocketThreadCallsAreAllRecorded)
{
  net::EditorConnections connections;
  constexpr int threadCount = 4;
  constexpr int idsPerThread = 200;
  constexpr int total = threadCount * idsPerThread;

  // A smoke test under ASan, not a race detector: the reader only exercises the locks while writers run.
  std::atomic<bool> writing{ true };
  std::thread reader([&connections, &writing]
  {
    while (writing.load())
    {
      static_cast<void>(connections.editorIds());
      static_cast<void>(connections.isEditor(0));
    }
  });

  std::vector<std::thread> threads;
  for (int t = 0; t < threadCount; ++t)
  {
    threads.emplace_back([&connections, t]
    {
      for (int i = 0; i < idsPerThread; ++i)
      {
        const int32_t id = t * idsPerThread + i;
        connections.authorize(id, editorRole);
        connections.enqueueDisconnect(id + 100000);
      }
    });
  }
  for (auto& thread : threads)
  {
    thread.join();
  }
  writing = false;
  reader.join();

  EXPECT_EQ(connections.editorIds().size(), static_cast<size_t>(total));
  EXPECT_TRUE(connections.isEditor(0));
  EXPECT_TRUE(connections.isEditor(total - 1));
  EXPECT_FALSE(connections.isEditor(total));
  EXPECT_EQ(connections.takeDisconnected().size(), static_cast<size_t>(total));
}
