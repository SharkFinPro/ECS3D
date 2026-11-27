#include <gtest/gtest.h>

#include "TerrainBrushSession.h"

#include "TerrainBrushSessionTestHelpers.h"
#include "edits/EditCommand.h"
#include "edits/RecordTerrainEdits.h"
#include "edits/TerrainSetChunks.h"
#include <terrain/TerrainBrush.h>
#include <terrain/TerrainChunk.h>
#include <terrain/TerrainRaycast.h>
#include <terrain/TerrainTypes.h>
#include <terrain/TerrainVolume.h>

#include <glm/vec3.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using namespace brushSessionTest;

namespace {
  struct StrokeRun
  {
    Env env;
    Recorder recorder;
    TerrainBrushSession session;
    terrain::TerrainVolume::ChunkMap pre;
    terrain::TerrainVolume::ChunkMap post;
    size_t brushTouched = 0;
    std::optional<TerrainStroke::Finished> finished;

    explicit StrokeRun(const glm::vec3 center, const float radius)
      : pre(env.volume->snapshotChunks())
    {
      EXPECT_TRUE(session.begin(terrainId, env.volume, templateOp(terrain::BrushOp::Kind::add, radius),
                                hitAt(center), recorder.sender()));
      session.moveTo(hitAt(center + glm::vec3(6.0f, 0.0f, 4.0f)), recorder.sender());
      brushTouched = session.touched().size();
      finished = session.end(recorder.sender());
      post = env.volume->snapshotChunks();
    }

    [[nodiscard]] size_t redistanceOnlyChunks() const
    {
      return finished ? finished->before.size() - brushTouched : 0;
    }
  };

  // Near the edge of the flat ground the release redistance changes a chunk the brush left alone.
  const glm::vec3 edgeCenter{ 8.0f, 10.0f, 8.0f };
  constexpr float edgeRadius = 6.0f;

  [[nodiscard]] std::shared_ptr<const terrain::TerrainChunk> chunkIn(const terrain::TerrainVolume::ChunkMap& chunks,
                                                                     const terrain::ChunkCoord coord)
  {
    const auto found = chunks.find(coord);
    return found == chunks.end() ? nullptr : found->second;
  }

  [[nodiscard]] bool volumeEquals(const terrain::TerrainVolume::ChunkMap& chunks, const terrain::TerrainVolume& other)
  {
    terrain::TerrainVolume copy(voxel);
    copy.restoreChunks(chunks);
    return sameChunks(copy, other);
  }
}

TEST(TerrainBrushSessionUndoTest, AChunkOnlyTheRedistanceChangedJoinsBeforeWithItsPreStrokePointer)
{
  const StrokeRun run(edgeCenter, edgeRadius);
  ASSERT_TRUE(run.finished.has_value());
  ASSERT_GT(run.redistanceOnlyChunks(), 0u);

  for (size_t index = run.brushTouched; index < run.finished->before.size(); ++index)
  {
    const auto& [coord, before] = run.finished->before[index];
    EXPECT_EQ(before, chunkIn(run.pre, coord));
    EXPECT_NE(before, chunkIn(run.post, coord));
  }
}

TEST(TerrainBrushSessionUndoTest, UndoingTheRecordedStrokeRestoresEveryChunkAndRedoReplaysIt)
{
  StrokeRun run(edgeCenter, edgeRadius);
  ASSERT_TRUE(run.finished.has_value());
  ASSERT_FALSE(volumeEquals(run.pre, *run.env.volume));

  const auto command = edits::commandForTerrainStroke(terrainId, run.finished->before, *run.env.volume);
  ASSERT_TRUE(command.has_value());

  edits::predictSetChunks(*run.env.volume, command->buildUndoTerrainChunks());
  EXPECT_TRUE(volumeEquals(run.pre, *run.env.volume));

  edits::predictSetChunks(*run.env.volume, command->buildRedoTerrainChunks());
  EXPECT_TRUE(volumeEquals(run.post, *run.env.volume));
}

TEST(TerrainBrushSessionUndoTest, WithoutTheRedistanceOnlyChunksAnUndoLeavesThemRedistanced)
{
  StrokeRun run(edgeCenter, edgeRadius);
  ASSERT_TRUE(run.finished.has_value());

  const std::span<const std::pair<terrain::ChunkCoord, std::shared_ptr<const terrain::TerrainChunk>>> brushOnly(
    run.finished->before.data(), run.brushTouched);
  const auto command = edits::commandForTerrainStroke(terrainId, brushOnly, *run.env.volume);
  ASSERT_TRUE(command.has_value());

  edits::predictSetChunks(*run.env.volume, command->buildUndoTerrainChunks());
  EXPECT_FALSE(volumeEquals(run.pre, *run.env.volume));
}

TEST(TerrainBrushAdvanceFinishedTest, AReleaseThatCommittedItsPredictionReturnsTheFinishedStroke)
{
  Frames frames;
  frames.frame(pressedAt(false), hitAt({ 40.0f, 10.0f, 40.0f }));
  frames.frame(pressedAt(true), hitAt({ 60.0f, 10.0f, 40.0f }));

  const auto held = frames.frame(pressedAt(true), hitAt({ 62.0f, 10.0f, 40.0f }));
  EXPECT_FALSE(held.finished.has_value());

  const auto released = frames.frame(makeTerrainPointer(false, true, true, false, false, true),
                                     hitAt({ 62.0f, 10.0f, 40.0f }));

  EXPECT_TRUE(released.ended);
  ASSERT_TRUE(released.finished.has_value());
  EXPECT_TRUE(released.finished->predicted);
  EXPECT_FALSE(released.finished->before.empty());
}

TEST(TerrainBrushAdvanceFinishedTest, AReplacedVolumeLeavesFinishedUnset)
{
  Frames frames;
  frames.frame(pressedAt(false), hitAt({ 40.0f, 10.0f, 40.0f }));
  frames.frame(pressedAt(true), hitAt({ 60.0f, 10.0f, 40.0f }));

  Env replacement;
  const auto result = frames.session.advance(terrainId, replacement.volume, true, pressedAt(true),
                                            hitAt({ 60.0f, 10.0f, 40.0f }), [] { return terrain::BrushOp{}; },
                                            frames.recorder.sender());

  EXPECT_TRUE(result.ended);
  EXPECT_FALSE(result.finished.has_value());
}

TEST(TerrainBrushAdvanceFinishedTest, LostEditingLeavesFinishedUnset)
{
  Frames frames;
  frames.frame(pressedAt(false), hitAt({ 40.0f, 10.0f, 40.0f }));
  frames.frame(pressedAt(true), hitAt({ 60.0f, 10.0f, 40.0f }));

  frames.editable = false;
  const auto released = frames.frame(makeTerrainPointer(false, true, true, false, false, true),
                                     hitAt({ 60.0f, 10.0f, 40.0f }));

  EXPECT_TRUE(released.ended);
  EXPECT_FALSE(released.finished.has_value());
}

TEST(TerrainBrushAdvanceFinishedTest, AStrokeThatChangedNothingLeavesFinishedUnset)
{
  const auto subtract = [] { return templateOp(terrain::BrushOp::Kind::subtract); };
  const auto release = makeTerrainPointer(false, true, true, false, false, true);

  // Carving air changes no chunk.
  Env env;
  Recorder recorder;
  TerrainBrushSession session;
  const terrain::RayHit overAir = hitAt({ 400.0f, 10.0f, 400.0f });
  session.advance(terrainId, env.volume, true, pressedAt(false), overAir, subtract, recorder.sender());
  ASSERT_TRUE(session.active());

  const auto released = session.advance(terrainId, env.volume, true, release, overAir, subtract, recorder.sender());
  EXPECT_TRUE(released.ended);
  EXPECT_FALSE(released.finished.has_value());

  // The control: the same stroke over the ground changes chunks and returns its finished stroke.
  Env ground;
  Recorder groundRecorder;
  TerrainBrushSession carving;
  const terrain::RayHit overGround = hitAt({ 40.0f, 10.0f, 40.0f });
  carving.advance(terrainId, ground.volume, true, pressedAt(false), overGround, subtract, groundRecorder.sender());
  const auto carved = carving.advance(terrainId, ground.volume, true, release, overGround, subtract,
                                      groundRecorder.sender());
  EXPECT_TRUE(carved.finished.has_value());
}
