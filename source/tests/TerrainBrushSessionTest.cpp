#include <gtest/gtest.h>

#include "TerrainBrushSession.h"

#include "TerrainBrushSessionTestHelpers.h"

#include <terrain/TerrainBrush.h>
#include <terrain/TerrainChunk.h>
#include <terrain/TerrainEdit.h>
#include <terrain/TerrainEditApply.h>
#include <terrain/TerrainGenerators.h>
#include <terrain/TerrainOps.h>
#include <terrain/TerrainRaycast.h>
#include <terrain/TerrainReplication.h>
#include <terrain/TerrainStore.h>
#include <terrain/TerrainTypes.h>
#include <terrain/TerrainVolume.h>
#include <EditorTerrainSettings.h>
#include <Protocol.h>

#include <glm/vec3.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>
#include <uuid.h>


using namespace brushSessionTest;

namespace {
  // After the brushes come one redistance per piece of the stroke's box that holds a touched chunk, in order.
  void expectRedistanceTail(const std::vector<terrain::TerrainEditPayload>& all, const size_t brushCount,
                            const TerrainStroke::Finished& finished)
  {
    std::vector<terrain::ChunkCoord> touched;
    for (const auto& entry : finished.before)
    {
      touched.push_back(entry.first);
    }

    const std::vector<terrain::SampleBox> pieces = redistanceBoxes(finished.box, touched);

    ASSERT_FALSE(pieces.empty());
    ASSERT_EQ(all.size(), brushCount + pieces.size());

    for (size_t index = 0; index < pieces.size(); ++index)
    {
      const terrain::TerrainEditPayload& payload = all[brushCount + index];
      EXPECT_EQ(payload.kind, terrain::TerrainEditKind::redistance);
      EXPECT_EQ(payload.box.min, pieces[index].min);
      EXPECT_EQ(payload.box.max, pieces[index].max);
    }

    for (const auto& payload : all)
    {
      EXPECT_EQ(payload.terrain, terrainId);
      EXPECT_EQ(payload.strokeId, finished.strokeId);
    }
  }

  // The coordinates whose chunk hash differs between the two maps; a missing chunk counts as different.
  std::unordered_set<terrain::ChunkCoord> changedBetween(const terrain::TerrainVolume::ChunkMap& before,
                                                         const terrain::TerrainVolume::ChunkMap& after)
  {
    std::unordered_set<terrain::ChunkCoord> changed;

    for (const auto& [coord, chunk] : after)
    {
      const auto match = before.find(coord);

      if (match == before.end() || match->second->hash() != chunk->hash())
      {
        changed.insert(coord);
      }
    }

    return changed;
  }

  // The release ends with one request for the server's final state of every touched chunk and every chunk the local
  // redistance changed, after the redistance, and for nothing else.
  void expectFinalChunkRequest(const net::Message& message, const TerrainStroke::Finished& finished,
                               const std::unordered_set<terrain::ChunkCoord>& redistanceChanged)
  {
    ASSERT_EQ(message.getType(), net::MessageType::terrainChunks);
    const auto request = terrain::unpackTerrainChunks(message);
    ASSERT_TRUE(request.has_value());
    EXPECT_TRUE(terrain::isChunkRequest(*request));
    EXPECT_EQ(request->terrain, terrainId);

    std::unordered_set<terrain::ChunkCoord> expected = redistanceChanged;
    for (const auto& entry : finished.before)
    {
      expected.insert(entry.first);
    }

    std::unordered_set<terrain::ChunkCoord> requested;
    for (const terrain::ReceivedChunk& wanted : request->entries)
    {
      requested.insert(wanted.coord);
    }

    EXPECT_EQ(request->entries.size(), requested.size());
    EXPECT_EQ(requested, expected);
  }

  // The coordinates the last message of the recorder requests.
  std::vector<terrain::ChunkCoord> requestedCoords(const Recorder& recorder)
  {
    std::vector<terrain::ChunkCoord> coords;
    const auto request = terrain::unpackTerrainChunks(recorder.sent.back());
    EXPECT_TRUE(request.has_value());

    if (request)
    {
      for (const terrain::ReceivedChunk& entry : request->entries)
      {
        coords.push_back(entry.coord);
      }
    }

    return coords;
  }

  // The server's full answer for the given chunks, read from the stroke's own volume.
  terrain::TerrainChunksPayload fullAnswer(const Env& env, const std::vector<terrain::ChunkCoord>& coords)
  {
    terrain::TerrainChunksPayload answer;
    answer.terrain = terrainId;

    for (const terrain::ChunkCoord& coord : coords)
    {
      terrain::ReceivedChunk entry;
      entry.coord = coord;
      entry.revision = 2;
      entry.chunk = env.volume->chunk(coord);
      entry.hash = entry.chunk ? entry.chunk->hash() : 0;
      answer.entries.push_back(entry);
    }

    return answer;
  }

  // A stroke dragged and released, leaving the session awaiting the server's final chunks.
  std::vector<terrain::ChunkCoord> releaseAwaiting(TerrainBrushSession& session, Env& env, Recorder& recorder)
  {
    drag(session, env, templateOp(terrain::BrushOp::Kind::add), recorder);
    EXPECT_TRUE(session.end(recorder.sender()).has_value());
    EXPECT_TRUE(session.awaitingFinalChunks());
    return requestedCoords(recorder);
  }
}

class TerrainBrushPredictionTest : public ::testing::TestWithParam<terrain::BrushOp::Kind> {};

TEST_P(TerrainBrushPredictionTest, ThePredictedVolumeMatchesWhatTheServerComputesFromTheSentMessages)
{
  Env predicted;
  Env server;
  const Env untouched;
  Recorder recorder;
  TerrainBrushSession session;

  drag(session, predicted, templateOp(GetParam()), recorder);
  ASSERT_TRUE(session.end(recorder.sender()).has_value());

  applyToServer(server, recorder.sent);

  EXPECT_FALSE(sameChunks(*predicted.volume, *untouched.volume));
  EXPECT_TRUE(sameChunks(*predicted.volume, *server.volume));
}

INSTANTIATE_TEST_SUITE_P(Kinds, TerrainBrushPredictionTest,
                         ::testing::Values(terrain::BrushOp::Kind::add, terrain::BrushOp::Kind::subtract,
                                           terrain::BrushOp::Kind::smooth, terrain::BrushOp::Kind::flatten));

TEST(TerrainBrushSessionTest, ThePredictionIncludesTheReleaseRedistance)
{
  Env predicted;
  Env withoutRedistance;
  Recorder recorder;
  TerrainBrushSession session;

  drag(session, predicted, templateOp(terrain::BrushOp::Kind::add), recorder);
  static_cast<void>(session.end(recorder.sender()));

  std::vector<net::Message> brushesOnly;

  for (const net::Message& message : recorder.sent)
  {
    if (message.getType() == net::MessageType::terrainEdit &&
        terrain::unpackTerrainEdit(message)->kind == terrain::TerrainEditKind::brush)
    {
      brushesOnly.push_back(message);
    }
  }

  ASSERT_LT(brushesOnly.size(), recorder.payloads().size());
  applyToServer(withoutRedistance, brushesOnly);

  EXPECT_FALSE(sameChunks(*predicted.volume, *withoutRedistance.volume));
}

TEST(TerrainBrushSessionTest, BrushEditsGoOutInOrderAndRedistanceOnlyAtRelease)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;

  drag(session, env, templateOp(terrain::BrushOp::Kind::add), recorder);

  const std::vector<terrain::TerrainEditPayload> duringDrag = recorder.payloads();
  ASSERT_GT(duringDrag.size(), 3u);

  for (const auto& payload : duringDrag)
  {
    EXPECT_EQ(payload.kind, terrain::TerrainEditKind::brush);
  }

  EXPECT_FLOAT_EQ(duringDrag.front().brush.center.x, 40.0f);
  EXPECT_FLOAT_EQ(duringDrag.front().brush.center.z, 40.0f);

  for (size_t index = 1; index < duringDrag.size(); ++index)
  {
    const glm::vec3 before = duringDrag[index - 1].brush.center;
    const glm::vec3 after = duringDrag[index].brush.center;
    EXPECT_TRUE(after.x > before.x || after.z > before.z);
  }

  const terrain::TerrainVolume::ChunkMap beforeRelease = env.volume->snapshotChunks();
  const std::optional<TerrainStroke::Finished> finished = session.end(recorder.sender());
  ASSERT_TRUE(finished.has_value());

  const std::vector<terrain::TerrainEditPayload> all = recorder.payloads();
  expectRedistanceTail(all, duringDrag.size(), *finished);
  expectFinalChunkRequest(recorder.sent.back(), *finished, changedBetween(beforeRelease, env.volume->snapshotChunks()));
}

TEST(TerrainBrushSessionTest, StrokeIdsIncreaseAndAreNeverZero)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;
  std::vector<uint64_t> ids;

  for (int stroke = 0; stroke < 3; ++stroke)
  {
    ASSERT_TRUE(session.begin(terrainId, env.volume, templateOp(terrain::BrushOp::Kind::add),
                              hitAt({ 40.0f + 10.0f * static_cast<float>(stroke), 10.0f, 40.0f }),
                              recorder.sender()));
    ids.push_back(session.end(recorder.sender())->strokeId);
  }

  EXPECT_GT(ids[0], 0u);
  EXPECT_LT(ids[0], ids[1]);
  EXPECT_LT(ids[1], ids[2]);
}

TEST(TerrainBrushSessionTest, ARadiusAboveTheCapIsClampedToWhatTheServerAccepts)
{
  Env env;
  Env control;
  Recorder recorder;
  TerrainBrushSession session;

  ASSERT_TRUE(session.begin(terrainId, env.volume, templateOp(terrain::BrushOp::Kind::add, 1000.0f),
                            hitAt({ 40.0f, 10.0f, 40.0f }), recorder.sender()));
  static_cast<void>(session.end(recorder.sender()));

  const std::vector<terrain::TerrainEditPayload> payloads = recorder.payloads();
  ASSERT_FALSE(payloads.empty());
  EXPECT_FLOAT_EQ(payloads.front().brush.radius, terrain::maxBrushRadiusVoxels * voxel);
  EXPECT_FALSE(sameChunks(*env.volume, *control.volume));

  applyToServer(control, recorder.sent);
  EXPECT_TRUE(sameChunks(*env.volume, *control.volume));
}

TEST(TerrainBrushSessionTest, ARadiusWithinTheCapIsSentAsIs)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;

  ASSERT_TRUE(session.begin(terrainId, env.volume, templateOp(terrain::BrushOp::Kind::add, 12.0f),
                            hitAt({ 40.0f, 10.0f, 40.0f }), recorder.sender()));

  const std::vector<terrain::TerrainEditPayload> payloads = recorder.payloads();
  ASSERT_EQ(payloads.size(), 1u);
  EXPECT_FLOAT_EQ(payloads.front().brush.radius, 12.0f);
}

TEST(TerrainBrushSessionTest, AStrokeWithNoApplicationsSendsNothingAtRelease)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;

  // Far outside what a volume addresses: the server would refuse the brush, so it is neither applied nor sent.
  EXPECT_TRUE(session.begin(terrainId, env.volume, templateOp(terrain::BrushOp::Kind::add),
                            hitAt({ 1.0e12f, 10.0f, 40.0f }), recorder.sender()));
  const std::optional<TerrainStroke::Finished> finished = session.end(recorder.sender());

  ASSERT_TRUE(finished.has_value());
  EXPECT_TRUE(finished->box.empty());
  EXPECT_TRUE(recorder.sent.empty());

  // The same stroke at a valid place sends its brush and its redistance.
  EXPECT_TRUE(session.begin(terrainId, env.volume, templateOp(terrain::BrushOp::Kind::add),
                            hitAt({ 40.0f, 10.0f, 40.0f }), recorder.sender()));
  static_cast<void>(session.end(recorder.sender()));
  EXPECT_GE(recorder.sent.size(), 2u);
}

TEST(TerrainBrushSessionTest, ANonFiniteHitBeginsNoStrokeAndEndingWithoutOneSendsNothing)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;

  EXPECT_FALSE(session.begin(terrainId, env.volume, templateOp(terrain::BrushOp::Kind::add),
                             hitAt({ std::numeric_limits<float>::quiet_NaN(), 10.0f, 40.0f }), recorder.sender()));
  EXPECT_FALSE(session.active());
  EXPECT_FALSE(session.end(recorder.sender()).has_value());
  EXPECT_TRUE(recorder.sent.empty());

  EXPECT_TRUE(session.begin(terrainId, env.volume, templateOp(terrain::BrushOp::Kind::add),
                            hitAt({ 40.0f, 10.0f, 40.0f }), recorder.sender()));
  EXPECT_TRUE(session.active());
}

TEST(TerrainBrushSessionTest, ASecondBeginWhileActiveIsRefused)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;

  ASSERT_TRUE(session.begin(terrainId, env.volume, templateOp(terrain::BrushOp::Kind::add),
                            hitAt({ 40.0f, 10.0f, 40.0f }), recorder.sender()));
  const size_t sentAfterFirst = recorder.sent.size();

  EXPECT_FALSE(session.begin(terrainId, env.volume, templateOp(terrain::BrushOp::Kind::add),
                             hitAt({ 60.0f, 10.0f, 40.0f }), recorder.sender()));
  EXPECT_EQ(recorder.sent.size(), sentAfterFirst);
}

TEST(TerrainBrushSessionTest, CancelDropsTheStrokeWithoutSendingARedistance)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;

  drag(session, env, templateOp(terrain::BrushOp::Kind::add), recorder);
  const size_t sentBeforeCancel = recorder.sent.size();

  session.cancel();

  EXPECT_FALSE(session.active());
  EXPECT_EQ(recorder.sent.size(), sentBeforeCancel);
  EXPECT_FALSE(session.end(recorder.sender()).has_value());
  EXPECT_EQ(recorder.sent.size(), sentBeforeCancel);
}

TEST(TerrainBrushSessionTest, ARefusedBeginKeepsTheWaitAndTheTerrainOfTheLastStroke)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;
  const auto other = uuids::uuid::from_string("22222222-2222-2222-2222-222222222222").value();
  releaseAwaiting(session, env, recorder);

  const float nan = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(session.begin(other, env.volume, templateOp(terrain::BrushOp::Kind::add), hitAt({ nan, 10.0f, 40.0f }),
                             recorder.sender()));
  EXPECT_TRUE(session.awaitingFinalChunks());
  EXPECT_EQ(session.terrain(), terrainId);
  EXPECT_NE(session.holdFor(terrainId), nullptr);

  EXPECT_TRUE(session.begin(other, env.volume, templateOp(terrain::BrushOp::Kind::add),
                            hitAt({ 40.0f, 10.0f, 40.0f }), recorder.sender()));
  EXPECT_EQ(session.terrain(), other);
  EXPECT_FALSE(session.awaitingFinalChunks());
}

TEST(TerrainBrushSessionTest, ABeginOnTheSameTerrainKeepsTheWait)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;
  releaseAwaiting(session, env, recorder);

  ASSERT_TRUE(session.begin(terrainId, env.volume, templateOp(terrain::BrushOp::Kind::add),
                            hitAt({ 40.0f, 10.0f, 40.0f }), recorder.sender()));
  EXPECT_TRUE(session.awaitingFinalChunks());
}

TEST(TerrainBrushSessionTest, CancelEndsTheWaitForAReleasedStroke)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;
  releaseAwaiting(session, env, recorder);

  session.cancel();

  EXPECT_FALSE(session.awaitingFinalChunks());
  EXPECT_EQ(session.holdFor(terrainId), nullptr);
}

TEST(TerrainBrushSessionTest, EndingASecondStrokeWithoutEditingEndsTheWait)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;
  releaseAwaiting(session, env, recorder);

  ASSERT_TRUE(session.begin(terrainId, env.volume, templateOp(terrain::BrushOp::Kind::add),
                            hitAt({ 40.0f, 10.0f, 40.0f }), recorder.sender()));
  ASSERT_TRUE(session.awaitingFinalChunks());
  const size_t sent = recorder.sent.size();

  EXPECT_FALSE(session.endFor(env.volume, false, recorder.sender()).has_value());

  EXPECT_FALSE(session.active());
  EXPECT_FALSE(session.awaitingFinalChunks());
  EXPECT_EQ(recorder.sent.size(), sent);
}

TEST(TerrainBrushSessionTest, AFullAnswerForEveryRequestedChunkEndsTheWait)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;
  const std::vector<terrain::ChunkCoord> requested = releaseAwaiting(session, env, recorder);
  ASSERT_GT(requested.size(), 1u);

  const std::vector<terrain::ChunkCoord> first(requested.begin(), requested.begin() + 1);
  session.confirmChunks(fullAnswer(env, first));
  EXPECT_TRUE(session.awaitingFinalChunks());

  session.confirmChunks(fullAnswer(env, requested));
  EXPECT_FALSE(session.awaitingFinalChunks());
  EXPECT_EQ(session.holdFor(terrainId), nullptr);
}

TEST(TerrainBrushSessionTest, AVolumeThatIsNoLongerTheOneTheStrokeFinishedOnEndsTheWait)
{
  Env env;
  Env replacement;
  Recorder recorder;
  TerrainBrushSession session;
  releaseAwaiting(session, env, recorder);

  session.dropAwaitingIfReplaced(env.volume);
  EXPECT_TRUE(session.awaitingFinalChunks());

  session.dropAwaitingIfReplaced(replacement.volume);
  EXPECT_FALSE(session.awaitingFinalChunks());
  EXPECT_EQ(session.holdFor(terrainId), nullptr);
}

TEST(TerrainBrushSessionTest, ARemovedTerrainEndsTheWait)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;
  releaseAwaiting(session, env, recorder);

  session.dropAwaitingIfReplaced(env.volume);
  ASSERT_TRUE(session.awaitingFinalChunks());

  session.dropAwaitingIfReplaced(nullptr);

  EXPECT_FALSE(session.awaitingFinalChunks());
}

TEST(TerrainBrushSessionTest, ANewLoadEndsTheWait)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;
  releaseAwaiting(session, env, recorder);
  ASSERT_NE(session.holdFor(terrainId), nullptr);

  session.clearAwaiting();

  EXPECT_FALSE(session.awaitingFinalChunks());
  EXPECT_EQ(session.holdFor(terrainId), nullptr);
}

TEST(TerrainBrushSessionTest, TheWaitHoldsUntilTheTimeoutAndThenExpires)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;
  releaseAwaiting(session, env, recorder);
  const auto now = std::chrono::steady_clock::now();

  session.expireAwaiting(now);
  EXPECT_TRUE(session.awaitingFinalChunks());

  session.expireAwaiting(now + TerrainBrushSession::awaitTimeout / 2);
  EXPECT_TRUE(session.awaitingFinalChunks());

  session.expireAwaiting(now + TerrainBrushSession::awaitTimeout + std::chrono::seconds(1));
  EXPECT_FALSE(session.awaitingFinalChunks());
  EXPECT_EQ(session.holdFor(terrainId), nullptr);
}

TEST(TerrainBrushSessionTest, TheStoreRevisionsStayAtTheConfirmedOnes)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;
  const terrain::ChunkCoord coord = terrain::chunkOfSample({ 16, 4, 16 });
  const uint32_t before = env.store.revision(terrainId, coord);
  ASSERT_GT(before, 0u);

  drag(session, env, templateOp(terrain::BrushOp::Kind::add), recorder);
  static_cast<void>(session.end(recorder.sender()));

  EXPECT_EQ(env.store.revision(terrainId, coord), before);
}

TEST(TerrainStrokeStepTest, AFreshPressOverTheSceneWithAHitBegins)
{
  TerrainPointer pointer;
  pointer.pressed = true;
  pointer.sceneHovered = true;
  pointer.hasHit = true;

  EXPECT_EQ(nextStrokeStep(false, pointer), TerrainStrokeStep::begin);
}

TEST(TerrainStrokeStepTest, NoBeginWithoutEachCondition)
{
  TerrainPointer valid;
  valid.pressed = true;
  valid.sceneHovered = true;
  valid.hasHit = true;
  ASSERT_EQ(nextStrokeStep(false, valid), TerrainStrokeStep::begin);

  TerrainPointer held = valid;
  held.wasPressed = true;
  EXPECT_EQ(nextStrokeStep(false, held), TerrainStrokeStep::none);

  TerrainPointer off = valid;
  off.sceneHovered = false;
  EXPECT_EQ(nextStrokeStep(false, off), TerrainStrokeStep::none);

  TerrainPointer flying = valid;
  flying.flying = true;
  EXPECT_EQ(nextStrokeStep(false, flying), TerrainStrokeStep::none);

  TerrainPointer miss = valid;
  miss.hasHit = false;
  EXPECT_EQ(nextStrokeStep(false, miss), TerrainStrokeStep::none);

  TerrainPointer idle = valid;
  idle.pressed = false;
  EXPECT_EQ(nextStrokeStep(false, idle), TerrainStrokeStep::none);
}

TEST(TerrainStrokeStepTest, AnActiveStrokeMovesWhileHeldAndEndsOnRelease)
{
  TerrainPointer held;
  held.pressed = true;
  held.wasPressed = true;
  held.sceneHovered = true;
  held.hasHit = true;
  EXPECT_EQ(nextStrokeStep(true, held), TerrainStrokeStep::move);

  TerrainPointer noHit = held;
  noHit.hasHit = false;
  EXPECT_EQ(nextStrokeStep(true, noHit), TerrainStrokeStep::none);

  TerrainPointer overPanel = held;
  overPanel.sceneHovered = false;
  EXPECT_EQ(nextStrokeStep(true, overPanel), TerrainStrokeStep::none);

  TerrainPointer released = held;
  released.pressed = false;
  EXPECT_EQ(nextStrokeStep(true, released), TerrainStrokeStep::end);
}

TEST(TerrainStrokeStepTest, AnActiveStrokeDoesNothingWhileFlyingButStillEndsOnRelease)
{
  TerrainPointer flying;
  flying.pressed = true;
  flying.wasPressed = true;
  flying.sceneHovered = true;
  flying.hasHit = true;
  flying.flying = true;
  EXPECT_EQ(nextStrokeStep(true, flying), TerrainStrokeStep::none);

  TerrainPointer notFlying = flying;
  notFlying.flying = false;
  EXPECT_EQ(nextStrokeStep(true, notFlying), TerrainStrokeStep::move);

  TerrainPointer released = flying;
  released.pressed = false;
  EXPECT_EQ(nextStrokeStep(true, released), TerrainStrokeStep::end);
}

TEST(TerrainPointerTest, ASceneCameraViewTakesThePointerAwayFromTheBrush)
{
  const TerrainPointer free = makeTerrainPointer(true, false, true, false, false, true);
  EXPECT_TRUE(free.sceneHovered);
  EXPECT_TRUE(free.pressed);
  EXPECT_FALSE(free.wasPressed);
  EXPECT_TRUE(free.hasHit);
  EXPECT_EQ(nextStrokeStep(false, free), TerrainStrokeStep::begin);

  const TerrainPointer viewing = makeTerrainPointer(true, false, true, true, false, true);
  EXPECT_FALSE(viewing.sceneHovered);
  EXPECT_EQ(nextStrokeStep(false, viewing), TerrainStrokeStep::none);

  EXPECT_FALSE(makeTerrainPointer(true, false, false, false, false, true).sceneHovered);
  EXPECT_TRUE(makeTerrainPointer(false, false, true, false, true, false).flying);
}

TEST(TerrainBrushHelpersTest, TheEffectiveRadiusIsCappedAtTheServerLimit)
{
  const terrain::TerrainVolume volume(voxel);
  const float cap = terrain::maxBrushRadiusVoxels * voxel;

  EXPECT_FLOAT_EQ(effectiveBrushRadius(10.0f, volume), 10.0f);
  EXPECT_FLOAT_EQ(effectiveBrushRadius(cap, volume), cap);
  EXPECT_FLOAT_EQ(effectiveBrushRadius(1000.0f, volume), cap);
}

TEST(TerrainBrushHelpersTest, TheTemplateCarriesTheToolKindAndTheSettings)
{
  editorTerrainSettings::BrushSettings brush;
  brush.radius = 7.0f;
  brush.strength = 0.3f;
  brush.falloff = 0.9f;

  const terrain::BrushOp smooth = terrainBrushTemplate(TerrainTool::smooth, brush);
  EXPECT_EQ(smooth.kind, terrain::BrushOp::Kind::smooth);
  EXPECT_FLOAT_EQ(smooth.radius, 7.0f);
  EXPECT_FLOAT_EQ(smooth.strength, 0.3f);
  EXPECT_FLOAT_EQ(smooth.falloff, 0.9f);

  EXPECT_EQ(terrainBrushTemplate(TerrainTool::subtract, brush).kind, terrain::BrushOp::Kind::subtract);
  EXPECT_EQ(terrainBrushTemplate(TerrainTool::flatten, brush).kind, terrain::BrushOp::Kind::flatten);
  EXPECT_EQ(terrainBrushTemplate(TerrainTool::add, brush).kind, terrain::BrushOp::Kind::add);
}
