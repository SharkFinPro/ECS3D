#include <gtest/gtest.h>

#include <TerrainUndoState.h>
#include <UndoRequestGate.h>
#include <edits/EditCommand.h>

#include <chrono>
#include <optional>
#include <string>
#include <uuid.h>

namespace {
  const uuids::uuid terrainA = uuids::uuid::from_string("11111111-1111-4111-8111-111111111111").value();
  const uuids::uuid terrainB = uuids::uuid::from_string("22222222-2222-4222-8222-222222222222").value();

  TerrainUndoContext awaiting(const std::optional<edits::CommandKind> kind, const std::optional<uuids::uuid>& uuid)
  {
    TerrainUndoContext context;
    context.awaitingFinalChunks = true;
    context.topKind = kind;
    context.topUuid = uuid;
    context.brushTerrain = terrainA;
    return context;
  }
}

TEST(TerrainUndoState, ARequestBindsTheTerrainItStagedWhenTheGateBegins)
{
  TerrainUndoState state;
  EXPECT_FALSE(state.pending().has_value());

  state.stage(terrainA);
  EXPECT_FALSE(state.pending().has_value());

  state.begin();
  ASSERT_TRUE(state.pending().has_value());
  EXPECT_EQ(*state.pending(), terrainA);
}

TEST(TerrainUndoState, AStructuralRequestDoesNotInheritAnEarlierTerrain)
{
  TerrainUndoState state;
  state.startRequest();
  state.stage(terrainA);
  state.begin();
  ASSERT_TRUE(state.pending().has_value());

  state.startRequest();
  state.begin();

  EXPECT_FALSE(state.pending().has_value());
  EXPECT_FALSE(state.noteTerrainChunks(terrainA, true));
}

TEST(TerrainUndoState, ARequestThatNeverBeganLeavesNothingStagedForTheNext)
{
  TerrainUndoState state;
  state.startRequest();
  state.stage(terrainA);

  state.startRequest();
  state.begin();

  EXPECT_FALSE(state.pending().has_value());
}

TEST(TerrainUndoState, OnlyTheChunksOfThePendingTerrainReleaseTheGate)
{
  TerrainUndoState state;
  state.stage(terrainA);
  state.begin();

  EXPECT_FALSE(state.noteTerrainChunks(terrainB, true));
  ASSERT_TRUE(state.pending().has_value());

  EXPECT_TRUE(state.noteTerrainChunks(terrainA, true));
  EXPECT_FALSE(state.pending().has_value());
  EXPECT_FALSE(state.noteTerrainChunks(terrainA, true));
}

TEST(TerrainUndoState, AGateThatTimedOutIsNotReleasedByALaterEcho)
{
  using std::chrono::milliseconds;
  UndoRequestGate::Clock::time_point now{};
  UndoRequestGate gate{ milliseconds(500), [&now] { return now; } };
  TerrainUndoState state;

  state.stage(terrainA);
  gate.begin();
  state.begin();
  now += milliseconds(600);
  ASSERT_FALSE(gate.blocked());

  EXPECT_FALSE(state.noteTerrainChunks(terrainA, gate.blocked()));
  EXPECT_FALSE(state.pending().has_value());

  // A later structural request begins the gate; the old terrain's echo must not release it.
  state.startRequest();
  gate.begin();
  state.begin();
  EXPECT_FALSE(state.noteTerrainChunks(terrainA, gate.blocked()));
  EXPECT_TRUE(gate.blocked());

  // Positive control: a terrain request on a live gate is released by its echo.
  state.startRequest();
  state.stage(terrainA);
  gate.begin();
  state.begin();
  EXPECT_TRUE(state.noteTerrainChunks(terrainA, gate.blocked()));
}

TEST(TerrainUndoState, ResetForgetsStagedAndPendingTerrain)
{
  TerrainUndoState state;
  state.stage(terrainA);
  state.begin();
  state.stage(terrainB);

  state.reset();
  state.begin();

  EXPECT_FALSE(state.pending().has_value());
}

TEST(TerrainUndoRefusal, AnActiveStrokeRefusesEveryKind)
{
  TerrainUndoContext context;
  EXPECT_EQ(decideTerrainUndoRefusal(context), TerrainUndoRefusal::none);

  context.strokeActive = true;
  EXPECT_EQ(decideTerrainUndoRefusal(context), TerrainUndoRefusal::strokeActive);

  context.topKind = edits::CommandKind::renameObject;
  context.topUuid = terrainB;
  EXPECT_EQ(decideTerrainUndoRefusal(context), TerrainUndoRefusal::strokeActive);
}

TEST(TerrainUndoRefusal, OutstandingFinalChunksRefuseOnlyAStrokeOfThatTerrain)
{
  EXPECT_EQ(decideTerrainUndoRefusal(awaiting(edits::CommandKind::terrainStroke, terrainA)),
            TerrainUndoRefusal::awaitingFinalTerrain);

  EXPECT_EQ(decideTerrainUndoRefusal(awaiting(edits::CommandKind::renameObject, terrainA)), TerrainUndoRefusal::none);
  EXPECT_EQ(decideTerrainUndoRefusal(awaiting(edits::CommandKind::terrainStroke, terrainB)), TerrainUndoRefusal::none);
  EXPECT_EQ(decideTerrainUndoRefusal(awaiting(std::nullopt, std::nullopt)), TerrainUndoRefusal::none);

  TerrainUndoContext settled = awaiting(edits::CommandKind::terrainStroke, terrainA);
  settled.awaitingFinalChunks = false;
  EXPECT_EQ(decideTerrainUndoRefusal(settled), TerrainUndoRefusal::none);
}

TEST(TerrainUndoRefusal, MessagesNameTheReason)
{
  EXPECT_EQ(terrainUndoRefusalMessage(TerrainUndoRefusal::strokeActive, true),
            "Can't undo: finish the terrain stroke first.");
  EXPECT_EQ(terrainUndoRefusalMessage(TerrainUndoRefusal::strokeActive, false),
            "Can't redo: finish the terrain stroke first.");
  EXPECT_EQ(terrainUndoRefusalMessage(TerrainUndoRefusal::awaitingFinalTerrain, true),
            "Can't undo: waiting for the server's final terrain.");
  EXPECT_TRUE(terrainUndoRefusalMessage(TerrainUndoRefusal::none, true).empty());
}
