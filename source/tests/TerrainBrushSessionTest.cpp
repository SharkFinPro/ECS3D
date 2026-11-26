#include <gtest/gtest.h>

#include "TerrainBrushSession.h"

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
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>
#include <uuid.h>

namespace {
  constexpr float voxel = 2.5f;

  const auto terrainId = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();

  terrain::RayHit hitAt(const glm::vec3 point)
  {
    terrain::RayHit hit;
    hit.point = point;
    hit.normal = { 0.0f, 1.0f, 0.0f };
    return hit;
  }

  terrain::BrushOp templateOp(const terrain::BrushOp::Kind kind, const float radius = 10.0f)
  {
    terrain::BrushOp op;
    op.kind = kind;
    op.radius = radius;
    op.strength = 0.8f;
    op.falloff = 0.4f;
    op.material = 1;
    return op;
  }

  struct Env
  {
    terrain::TerrainStore store;
    std::shared_ptr<terrain::TerrainVolume> volume;

    Env()
    {
      terrain::TerrainDescription description;
      description.uuid = terrainId;
      description.voxelSize = voxel;
      volume = store.create(description);

      const uint32_t revision = store.nextRevision(terrainId);
      for (const auto& [coord, chunk] : terrain::fillFlat(*volume, { 64, 64 }, 10.0f, 20.0f, 1))
      {
        store.setChunk(terrainId, coord, chunk, revision);
      }
    }
  };

  struct Recorder
  {
    std::vector<net::Message> sent;

    TerrainBrushSession::Send sender()
    {
      return [this](const net::Message& message) { sent.push_back(message); };
    }

    [[nodiscard]] std::vector<terrain::TerrainEditPayload> payloads() const
    {
      std::vector<terrain::TerrainEditPayload> decoded;

      for (const net::Message& message : sent)
      {
        if (message.getType() != net::MessageType::terrainEdit)
        {
          continue;
        }

        const auto payload = terrain::unpackTerrainEdit(message);
        EXPECT_TRUE(payload.has_value());

        if (payload)
        {
          decoded.push_back(*payload);
        }
      }

      return decoded;
    }
  };

  // The stroke a pointer would draw: press, then a bent drag across the flat ground. A flatten captures its plane at
  // the press, so it starts above the ground to have something to cut.
  void drag(TerrainBrushSession& session, Env& env, const terrain::BrushOp& op, Recorder& recorder)
  {
    const float pressHeight = op.kind == terrain::BrushOp::Kind::flatten ? 14.0f : 10.0f;

    ASSERT_TRUE(session.begin(terrainId, env.volume, op, hitAt({ 40.0f, pressHeight, 40.0f }), recorder.sender()));
    session.moveTo(hitAt({ 60.0f, 10.0f, 40.0f }), recorder.sender());
    session.moveTo(hitAt({ 60.0f, 10.0f, 70.0f }), recorder.sender());
  }

  void applyToServer(Env& server, const std::vector<net::Message>& messages)
  {
    for (const net::Message& message : messages)
    {
      if (message.getType() != net::MessageType::terrainEdit)
      {
        continue;
      }

      const auto payload = terrain::unpackTerrainEdit(message);
      ASSERT_TRUE(payload.has_value());
      EXPECT_EQ(terrain::applyTerrainEdit(server.store, *payload).refusal, terrain::TerrainEditRefusal::none);
    }
  }

  // Equal when every chunk of either volume exists in the other with the same hash.
  bool sameChunks(const terrain::TerrainVolume& a, const terrain::TerrainVolume& b)
  {
    const terrain::TerrainVolume::ChunkMap left = a.snapshotChunks();
    const terrain::TerrainVolume::ChunkMap right = b.snapshotChunks();

    if (left.size() != right.size())
    {
      return false;
    }

    for (const auto& [coord, chunk] : left)
    {
      const auto match = right.find(coord);

      if (match == right.end() || match->second->hash() != chunk->hash())
      {
        return false;
      }
    }

    return true;
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

  const std::optional<TerrainStroke::Finished> finished = session.end(recorder.sender());
  ASSERT_TRUE(finished.has_value());

  const std::vector<terrain::TerrainEditPayload> all = recorder.payloads();
  std::vector<terrain::ChunkCoord> touched;
  for (const auto& entry : finished->before)
  {
    touched.push_back(entry.first);
  }

  const std::vector<terrain::SampleBox> pieces = redistanceBoxes(finished->box, touched);

  ASSERT_FALSE(pieces.empty());
  ASSERT_EQ(all.size(), duringDrag.size() + pieces.size());

  for (size_t index = 0; index < pieces.size(); ++index)
  {
    const terrain::TerrainEditPayload& payload = all[duringDrag.size() + index];
    EXPECT_EQ(payload.kind, terrain::TerrainEditKind::redistance);
    EXPECT_EQ(payload.box.min, pieces[index].min);
    EXPECT_EQ(payload.box.max, pieces[index].max);
  }

  for (const auto& payload : all)
  {
    EXPECT_EQ(payload.terrain, terrainId);
    EXPECT_EQ(payload.strokeId, finished->strokeId);
  }

  // The release ends with one request for the server's final state of every touched chunk, after the redistance.
  ASSERT_EQ(recorder.sent.back().getType(), net::MessageType::terrainChunks);
  const auto request = terrain::unpackTerrainChunks(recorder.sent.back());
  ASSERT_TRUE(request.has_value());
  EXPECT_TRUE(terrain::isChunkRequest(*request));
  EXPECT_EQ(request->terrain, terrainId);
  ASSERT_EQ(request->entries.size(), finished->before.size());

  for (const auto& entry : finished->before)
  {
    const bool listed = std::any_of(request->entries.begin(), request->entries.end(),
                                    [&entry](const terrain::ReceivedChunk& wanted) { return wanted.coord == entry.first; });
    EXPECT_TRUE(listed);
  }
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

TEST(TerrainBrushSessionTest, EndingForAReplacedVolumeSendsTheRedistanceButLeavesTheOldVolumeAlone)
{
  Env env;
  Env control;
  Recorder replaced;
  Recorder normal;
  TerrainBrushSession session;
  TerrainBrushSession controlSession;

  drag(session, env, templateOp(terrain::BrushOp::Kind::add), replaced);
  drag(controlSession, control, templateOp(terrain::BrushOp::Kind::add), normal);
  const terrain::TerrainVolume::ChunkMap beforeEnd = env.volume->snapshotChunks();
  const terrain::TerrainVolume::ChunkMap controlBeforeEnd = control.volume->snapshotChunks();

  const Env other;
  ASSERT_TRUE(session.endFor(other.volume, true, replaced.sender()).has_value());
  ASSERT_TRUE(controlSession.endFor(control.volume, true, normal.sender()).has_value());

  EXPECT_FALSE(session.active());

  terrain::TerrainVolume untouchedCopy(voxel);
  untouchedCopy.restoreChunks(beforeEnd);
  EXPECT_TRUE(sameChunks(*env.volume, untouchedCopy));

  terrain::TerrainVolume controlCopy(voxel);
  controlCopy.restoreChunks(controlBeforeEnd);
  EXPECT_FALSE(sameChunks(*control.volume, controlCopy));

  const std::vector<terrain::TerrainEditPayload> replacedPayloads = replaced.payloads();
  const std::vector<terrain::TerrainEditPayload> normalPayloads = normal.payloads();
  ASSERT_EQ(replacedPayloads.size(), normalPayloads.size());
  ASSERT_GT(replacedPayloads.size(), 0u);
  EXPECT_EQ(replacedPayloads.back().kind, terrain::TerrainEditKind::redistance);

  // No request for the final state of a volume nobody holds any more; the same-volume end asks for it.
  EXPECT_EQ(replaced.sent.size(), replacedPayloads.size());
  EXPECT_GT(normal.sent.size(), normalPayloads.size());
}

TEST(TerrainBrushSessionTest, EndingWithoutEditingDropsTheStrokeAndSendsNothing)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;

  drag(session, env, templateOp(terrain::BrushOp::Kind::add), recorder);
  const size_t sentWhileDragging = recorder.sent.size();

  EXPECT_FALSE(session.endFor(env.volume, false, recorder.sender()).has_value());
  EXPECT_FALSE(session.active());
  EXPECT_EQ(recorder.sent.size(), sentWhileDragging);

  // The editable path of the same stroke sends its redistance.
  Env other;
  Recorder editable;
  TerrainBrushSession again;
  drag(again, other, templateOp(terrain::BrushOp::Kind::add), editable);
  const size_t before = editable.sent.size();
  EXPECT_TRUE(again.endFor(other.volume, true, editable.sender()).has_value());
  EXPECT_GT(editable.sent.size(), before);
}

namespace {
  TerrainPointer pressedAt(const bool wasPressed, const bool flying = false)
  {
    return makeTerrainPointer(true, wasPressed, true, false, flying, false);
  }

  struct Frames
  {
    Env env;
    Recorder recorder;
    TerrainBrushSession session;
    int templateCalls = 0;
    bool editable = true;

    TerrainBrushSession::Advance frame(const TerrainPointer& pointer, const std::optional<terrain::RayHit>& hit)
    {
      return session.advance(terrainId, env.volume, editable, pointer, hit,
                             [this] {
                               ++templateCalls;
                               return templateOp(terrain::BrushOp::Kind::add);
                             },
                             recorder.sender());
    }

    size_t brushCount() const
    {
      size_t count = 0;

      for (const auto& payload : recorder.payloads())
      {
        count += payload.kind == terrain::TerrainEditKind::brush ? 1 : 0;
      }

      return count;
    }
  };
}

TEST(TerrainBrushAdvanceTest, ThePressReadsTheTemplateOnceAndTheDragPaintsWithIt)
{
  Frames frames;

  EXPECT_TRUE(frames.frame(pressedAt(false), hitAt({ 40.0f, 10.0f, 40.0f })).changed);
  EXPECT_TRUE(frames.frame(pressedAt(true), hitAt({ 60.0f, 10.0f, 40.0f })).changed);
  EXPECT_TRUE(frames.frame(pressedAt(true), hitAt({ 70.0f, 10.0f, 40.0f })).changed);

  EXPECT_EQ(frames.templateCalls, 1);
  EXPECT_GT(frames.brushCount(), 3u);

  // Not pressed: nothing begins, so the template is not read.
  Frames idle;
  const TerrainPointer hover = makeTerrainPointer(false, false, true, false, false, true);
  EXPECT_FALSE(idle.frame(hover, hitAt({ 40.0f, 10.0f, 40.0f })).changed);
  EXPECT_EQ(idle.templateCalls, 0);
}

TEST(TerrainBrushAdvanceTest, ReleaseEndsTheStrokeAndSendsTheRedistance)
{
  Frames frames;
  frames.frame(pressedAt(false), hitAt({ 40.0f, 10.0f, 40.0f }));
  frames.frame(pressedAt(true), hitAt({ 60.0f, 10.0f, 40.0f }));
  const size_t brushes = frames.recorder.sent.size();

  const auto released = frames.frame(makeTerrainPointer(false, true, true, false, false, true),
                                     hitAt({ 60.0f, 10.0f, 40.0f }));

  EXPECT_TRUE(released.ended);
  EXPECT_FALSE(frames.session.active());
  EXPECT_GT(frames.recorder.sent.size(), brushes);
}

TEST(TerrainBrushAdvanceTest, ALostHitDoesNotBridgeTheGapWhenThePointerComesBack)
{
  Frames gap;
  gap.frame(pressedAt(false), hitAt({ 40.0f, 10.0f, 40.0f }));
  gap.frame(pressedAt(true), std::nullopt);
  const size_t beforeReturn = gap.brushCount();
  gap.frame(pressedAt(true), hitAt({ 120.0f, 10.0f, 40.0f }));

  EXPECT_EQ(gap.brushCount() - beforeReturn, 1u);

  Frames bridged;
  bridged.frame(pressedAt(false), hitAt({ 40.0f, 10.0f, 40.0f }));
  const size_t beforeJump = bridged.brushCount();
  bridged.frame(pressedAt(true), hitAt({ 120.0f, 10.0f, 40.0f }));

  EXPECT_GT(bridged.brushCount() - beforeJump, 3u);
}

TEST(TerrainBrushAdvanceTest, FlyingPaintsNothingBreaksThePathAndReleaseStillEnds)
{
  Frames frames;
  frames.frame(pressedAt(false), hitAt({ 40.0f, 10.0f, 40.0f }));
  const size_t afterPress = frames.brushCount();

  const auto whileFlying = frames.frame(pressedAt(true, true), hitAt({ 120.0f, 10.0f, 40.0f }));
  EXPECT_FALSE(whileFlying.changed);
  EXPECT_FALSE(whileFlying.ended);
  EXPECT_TRUE(frames.session.active());
  EXPECT_EQ(frames.brushCount(), afterPress);

  frames.frame(pressedAt(true), hitAt({ 140.0f, 10.0f, 40.0f }));
  EXPECT_EQ(frames.brushCount(), afterPress + 1);

  EXPECT_TRUE(frames.frame(makeTerrainPointer(false, true, true, false, true, true), std::nullopt).ended);
}

TEST(TerrainBrushAdvanceTest, AReplacedVolumeEndsTheStrokeWithItsRedistance)
{
  Frames frames;
  frames.frame(pressedAt(false), hitAt({ 40.0f, 10.0f, 40.0f }));
  frames.frame(pressedAt(true), hitAt({ 60.0f, 10.0f, 40.0f }));
  const size_t sentBefore = frames.recorder.sent.size();

  Env replacement;
  const auto result = frames.session.advance(terrainId, replacement.volume, true, pressedAt(true),
                                            hitAt({ 60.0f, 10.0f, 40.0f }), [] { return terrain::BrushOp{}; },
                                            frames.recorder.sender());

  EXPECT_TRUE(result.ended);
  EXPECT_FALSE(frames.session.active());
  ASSERT_GT(frames.recorder.sent.size(), sentBefore);
  EXPECT_EQ(frames.recorder.payloads().back().kind, terrain::TerrainEditKind::redistance);
}
