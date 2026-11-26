#include <gtest/gtest.h>

#include "TerrainBrushSession.h"

#include "TerrainBrushSessionTestHelpers.h"
#include <terrain/TerrainBrush.h>
#include <terrain/TerrainEdit.h>
#include <terrain/TerrainRaycast.h>
#include <terrain/TerrainVolume.h>
#include <Protocol.h>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <vector>

using namespace brushSessionTest;

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

TEST(TerrainBrushAdvanceTest, WithoutEditingNoStrokeBeginsAndNothingIsPredictedOrSent)
{
  Frames frames;
  frames.editable = false;

  const auto refused = frames.frame(pressedAt(false), hitAt({ 40.0f, 10.0f, 40.0f }));

  EXPECT_FALSE(refused.changed);
  EXPECT_FALSE(frames.session.active());
  EXPECT_EQ(frames.templateCalls, 0);
  EXPECT_TRUE(frames.recorder.sent.empty());

  const Env untouched;
  EXPECT_TRUE(sameChunks(*frames.env.volume, *untouched.volume));

  // The control: the same press with editing allowed begins and sends.
  Frames editable;
  EXPECT_TRUE(editable.frame(pressedAt(false), hitAt({ 40.0f, 10.0f, 40.0f })).changed);
  EXPECT_TRUE(editable.session.active());
  EXPECT_FALSE(editable.recorder.sent.empty());
}

TEST(TerrainBrushSessionTest, ASendThatThrowsAtReleaseLeavesTheSessionIdleAndLosesTheRest)
{
  Env env;
  Recorder recorder;
  TerrainBrushSession session;
  drag(session, env, templateOp(terrain::BrushOp::Kind::add), recorder);

  size_t calls = 0;
  const TerrainBrushSession::Send failing = [&calls](const net::Message&) {
    ++calls;
    throw std::runtime_error("connection lost");
  };

  EXPECT_THROW(static_cast<void>(session.end(failing)), std::runtime_error);
  EXPECT_EQ(calls, 1u);
  EXPECT_FALSE(session.active());
  EXPECT_EQ(session.volume(), nullptr);
  EXPECT_TRUE(session.touched().empty());

  // The control: a session that ended normally sent more than that first message.
  Env other;
  Recorder normal;
  TerrainBrushSession again;
  drag(again, other, templateOp(terrain::BrushOp::Kind::add), normal);
  const size_t beforeEnd = normal.sent.size();
  static_cast<void>(again.end(normal.sender()));
  EXPECT_GT(normal.sent.size() - beforeEnd, 1u);

  // The failed session takes a new stroke.
  EXPECT_TRUE(session.begin(terrainId, env.volume, templateOp(terrain::BrushOp::Kind::add),
                            hitAt({ 40.0f, 10.0f, 40.0f }), recorder.sender()));
}
