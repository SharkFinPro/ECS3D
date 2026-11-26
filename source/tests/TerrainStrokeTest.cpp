#include <gtest/gtest.h>

#include "TerrainStroke.h"

#include <terrain/TerrainBrush.h>
#include <terrain/TerrainChunk.h>
#include <terrain/TerrainGenerators.h>
#include <terrain/TerrainOps.h>
#include <terrain/TerrainRaycast.h>
#include <terrain/TerrainTypes.h>
#include <terrain/TerrainVolume.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {
  constexpr float radius = 4.0f;
  constexpr float voxel = 2.5f;
  constexpr float expectedSpacing = 1.0f;

  terrain::RayHit hitAt(const glm::vec3 point, const glm::vec3 normal = { 0.0f, 1.0f, 0.0f })
  {
    terrain::RayHit hit;
    hit.point = point;
    hit.normal = normal;
    return hit;
  }

  terrain::BrushOp templateOp(const terrain::BrushOp::Kind kind = terrain::BrushOp::Kind::add)
  {
    terrain::BrushOp op;
    op.kind = kind;
    op.radius = radius;
    op.strength = 0.7f;
    op.falloff = 0.3f;
    op.material = 3;
    return op;
  }

  terrain::TerrainVolume flatGround()
  {
    terrain::TerrainVolume volume(voxel);
    terrain::commit(volume, terrain::fillFlat(volume, { 64, 64 }, 11.0f, 20.0f, 1));
    return volume;
  }

  void applyAndRecord(TerrainStroke& stroke, terrain::TerrainVolume& volume, const terrain::BrushOp& op)
  {
    const terrain::ChunkEdits edits = terrain::applyBrush(volume, op);
    stroke.recordApplication(volume, op, edits);
    terrain::commit(volume, edits);
  }

  terrain::BrushOp digOp(const glm::vec3 center)
  {
    terrain::BrushOp op = templateOp(terrain::BrushOp::Kind::subtract);
    op.radius = 10.0f;
    op.strength = 1.0f;
    op.falloff = 0.0f;
    op.center = center;
    return op;
  }

  size_t recordsFor(const TerrainStroke::Finished& finished, const terrain::ChunkCoord coord)
  {
    return static_cast<size_t>(std::count_if(finished.before.begin(), finished.before.end(),
                                             [&](const auto& entry) { return entry.first == coord; }));
  }

  bool covered(const std::vector<terrain::SampleBox>& pieces, const glm::ivec3 sample)
  {
    return std::any_of(pieces.begin(), pieces.end(), [&](const terrain::SampleBox& piece)
    {
      return sample.x >= piece.min.x && sample.x < piece.max.x && sample.y >= piece.min.y &&
             sample.y < piece.max.y && sample.z >= piece.min.z && sample.z < piece.max.z;
    });
  }

  size_t volumeOf(const terrain::SampleBox& box)
  {
    const glm::ivec3 size = box.max - box.min;
    return static_cast<size_t>(size.x) * static_cast<size_t>(size.y) * static_cast<size_t>(size.z);
  }
}

TEST(TerrainStrokeTest, StationaryPressYieldsExactlyOneApplication)
{
  TerrainStroke stroke;
  EXPECT_FALSE(stroke.active());

  const terrain::BrushOp first = stroke.begin(7, templateOp(), hitAt({ 10.0f, 2.0f, 5.0f }));

  EXPECT_TRUE(stroke.active());
  EXPECT_EQ(stroke.strokeId(), 7u);
  EXPECT_EQ(first.center, glm::vec3(10.0f, 2.0f, 5.0f));
  EXPECT_EQ(first.radius, radius);
  EXPECT_EQ(first.material, 3);
  EXPECT_TRUE(stroke.moveTo(hitAt({ 10.0f, 2.0f, 5.0f })).empty());
  EXPECT_TRUE(stroke.moveTo(hitAt({ 10.0f, 2.0f, 5.0f })).empty());
}

TEST(TerrainStrokeTest, MoveToWithoutAStrokeIsEmpty)
{
  TerrainStroke stroke;

  EXPECT_TRUE(stroke.moveTo(hitAt({ 100.0f, 0.0f, 0.0f })).empty());

  stroke.begin(1, templateOp(), hitAt({ 0.0f, 0.0f, 0.0f }));
  EXPECT_FALSE(stroke.moveTo(hitAt({ 100.0f, 0.0f, 0.0f })).empty());
}

TEST(TerrainStrokeTest, SpacingIsAQuarterOfTheRadiusWithAPositiveFloor)
{
  TerrainStroke stroke;
  stroke.begin(1, templateOp(), hitAt({ 0.0f, 0.0f, 0.0f }));
  EXPECT_FLOAT_EQ(stroke.spacing(), expectedSpacing);

  terrain::BrushOp zero = templateOp();
  zero.radius = 0.0f;
  TerrainStroke tiny;
  tiny.begin(1, zero, hitAt({ 0.0f, 0.0f, 0.0f }));
  EXPECT_GT(tiny.spacing(), 0.0f);
  EXPECT_LE(tiny.moveTo(hitAt({ 1000.0f, 0.0f, 0.0f })).size(), TerrainStroke::maxApplicationsPerMove);
}

TEST(TerrainStrokeTest, StraightDragYieldsFourApplicationsPerRadiusCarriedAcrossCalls)
{
  TerrainStroke stroke;
  stroke.begin(1, templateOp(), hitAt({ 0.0f, 0.0f, 0.0f }));

  std::vector<terrain::BrushOp> all;

  for (const float x : { 0.7f, 3.1f, 3.2f, 7.9f, 8.0f, 11.3f, 12.0f })
  {
    const std::vector<terrain::BrushOp> due = stroke.moveTo(hitAt({ x, 0.0f, 0.0f }));
    all.insert(all.end(), due.begin(), due.end());
  }

  ASSERT_EQ(all.size(), 12u);

  for (size_t i = 0; i < all.size(); ++i)
  {
    EXPECT_NEAR(all[i].center.x, static_cast<float>(i + 1) * expectedSpacing, 1.0e-4f);
    EXPECT_NEAR(all[i].center.y, 0.0f, 1.0e-6f);
  }

  TerrainStroke oneCall;
  oneCall.begin(1, templateOp(), hitAt({ 0.0f, 0.0f, 0.0f }));
  EXPECT_EQ(oneCall.moveTo(hitAt({ 12.0f, 0.0f, 0.0f })).size(), all.size());
}

TEST(TerrainStrokeTest, LeftoverDistanceCarriesAcrossCalls)
{
  TerrainStroke stroke;
  stroke.begin(1, templateOp(), hitAt({ 0.0f, 0.0f, 0.0f }));

  EXPECT_TRUE(stroke.moveTo(hitAt({ 0.6f, 0.0f, 0.0f })).empty());

  const std::vector<terrain::BrushOp> due = stroke.moveTo(hitAt({ 1.2f, 0.0f, 0.0f }));

  ASSERT_EQ(due.size(), 1u);
  EXPECT_NEAR(due[0].center.x, 1.0f, 1.0e-5f);
}

TEST(TerrainStrokeTest, CornerMeasuresAlongThePathNotTheChord)
{
  TerrainStroke stroke;
  stroke.begin(1, templateOp(), hitAt({ 0.0f, 0.0f, 0.0f }));

  std::vector<terrain::BrushOp> all = stroke.moveTo(hitAt({ 4.0f, 0.0f, 0.0f }));
  const std::vector<terrain::BrushOp> second = stroke.moveTo(hitAt({ 4.0f, 0.0f, 4.0f }));
  all.insert(all.end(), second.begin(), second.end());

  ASSERT_EQ(all.size(), 8u);
  EXPECT_NEAR(all[3].center.x, 4.0f, 1.0e-4f);
  EXPECT_NEAR(all[3].center.z, 0.0f, 1.0e-4f);
  EXPECT_NEAR(all[7].center.x, 4.0f, 1.0e-4f);
  EXPECT_NEAR(all[7].center.z, 4.0f, 1.0e-4f);

  for (size_t i = 4; i < all.size(); ++i)
  {
    EXPECT_NEAR(glm::length(all[i].center - all[i - 1].center), 1.0f, 1.0e-4f);
  }
}

TEST(TerrainStrokeTest, ACappedJumpKeepsTheLastApplicationsSoTheBrushEndsAtThePointer)
{
  TerrainStroke stroke;
  stroke.begin(1, templateOp(), hitAt({ 0.0f, 0.0f, 0.0f }));

  const std::vector<terrain::BrushOp> due = stroke.moveTo(hitAt({ 1000.0f, 0.0f, 0.0f }));

  ASSERT_EQ(due.size(), TerrainStroke::maxApplicationsPerMove);

  for (size_t i = 0; i < due.size(); ++i)
  {
    EXPECT_NEAR(due[i].center.x, 745.0f + static_cast<float>(i), 1.0e-3f);
  }

  const std::vector<terrain::BrushOp> next = stroke.moveTo(hitAt({ 1002.0f, 0.0f, 0.0f }));
  ASSERT_EQ(next.size(), 2u);
  EXPECT_NEAR(next[0].center.x, 1001.0f, 1.0e-3f);
  EXPECT_NEAR(next[1].center.x, 1002.0f, 1.0e-3f);
}

TEST(TerrainStrokeTest, ACappedJumpCarriesTheLeftoverDistance)
{
  TerrainStroke stroke;
  stroke.begin(1, templateOp(), hitAt({ 0.0f, 0.0f, 0.0f }));

  const std::vector<terrain::BrushOp> due = stroke.moveTo(hitAt({ 1000.5f, 0.0f, 0.0f }));
  ASSERT_EQ(due.size(), TerrainStroke::maxApplicationsPerMove);
  EXPECT_NEAR(due.back().center.x, 1000.0f, 1.0e-3f);

  const std::vector<terrain::BrushOp> next = stroke.moveTo(hitAt({ 1001.0f, 0.0f, 0.0f }));
  ASSERT_EQ(next.size(), 1u);
  EXPECT_NEAR(next[0].center.x, 1001.0f, 1.0e-3f);
}

TEST(TerrainStrokeTest, BeginningWhileActiveDiscardsTheOldRecord)
{
  terrain::TerrainVolume volume = flatGround();
  const terrain::ChunkCoord groundCoord{ 0, 0, 0 };

  TerrainStroke stroke;
  applyAndRecord(stroke, volume, stroke.begin(1, digOp({ 30.0f, 11.0f, 30.0f }), hitAt({ 30.0f, 11.0f, 30.0f })));

  stroke.begin(2, templateOp(), hitAt({ 0.0f, 0.0f, 0.0f }));
  const std::optional<TerrainStroke::Finished> finished = stroke.end();

  ASSERT_TRUE(finished.has_value());
  EXPECT_EQ(finished->strokeId, 2u);
  EXPECT_EQ(recordsFor(*finished, groundCoord), 0u);
  EXPECT_TRUE(finished->box.empty());
}

TEST(TerrainStrokeTest, ModerateJumpBelowTheCapIsNotTruncated)
{
  TerrainStroke stroke;
  stroke.begin(1, templateOp(), hitAt({ 0.0f, 0.0f, 0.0f }));

  EXPECT_EQ(stroke.moveTo(hitAt({ 100.0f, 0.0f, 0.0f })).size(), 100u);
}

TEST(TerrainStrokeTest, NonFiniteHitsAreGuarded)
{
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();

  TerrainStroke refused;
  const terrain::BrushOp base = templateOp();
  const terrain::BrushOp returned = refused.begin(4, base, hitAt({ nan, 0.0f, 0.0f }));

  EXPECT_FALSE(refused.active());
  EXPECT_EQ(returned.center, base.center);
  EXPECT_EQ(returned.radius, base.radius);
  EXPECT_FALSE(refused.end().has_value());

  TerrainStroke stroke;
  stroke.begin(5, templateOp(), hitAt({ 0.0f, 0.0f, 0.0f }));
  EXPECT_TRUE(stroke.moveTo(hitAt({ inf, 0.0f, 0.0f })).empty());
  EXPECT_TRUE(stroke.moveTo(hitAt({ 0.0f, nan, 0.0f })).empty());
  EXPECT_EQ(stroke.moveTo(hitAt({ 2.0f, 0.0f, 0.0f })).size(), 2u);
}

TEST(TerrainStrokeTest, FlattenKeepsThePlaneFromTheFirstHit)
{
  TerrainStroke stroke;
  const terrain::BrushOp first =
    stroke.begin(1, templateOp(terrain::BrushOp::Kind::flatten), hitAt({ 1.0f, 2.0f, 3.0f }, { 0.0f, 3.0f, 0.0f }));

  EXPECT_EQ(first.planePoint, glm::vec3(1.0f, 2.0f, 3.0f));
  EXPECT_NEAR(first.planeNormal.y, 1.0f, 1.0e-6f);

  const std::vector<terrain::BrushOp> due = stroke.moveTo(hitAt({ 9.0f, 8.0f, 7.0f }, { 1.0f, 0.0f, 0.0f }));

  ASSERT_FALSE(due.empty());

  for (const terrain::BrushOp& op : due)
  {
    EXPECT_EQ(op.planePoint, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_NEAR(op.planeNormal.x, 0.0f, 1.0e-6f);
    EXPECT_NEAR(op.planeNormal.y, 1.0f, 1.0e-6f);
    EXPECT_EQ(op.kind, terrain::BrushOp::Kind::flatten);
  }
}

TEST(TerrainStrokeTest, FlattenFallsBackToUpForADegenerateNormal)
{
  TerrainStroke zero;
  EXPECT_EQ(zero.begin(1, templateOp(terrain::BrushOp::Kind::flatten), hitAt({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }))
              .planeNormal,
            glm::vec3(0.0f, 1.0f, 0.0f));

  TerrainStroke bad;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  EXPECT_EQ(bad.begin(1, templateOp(terrain::BrushOp::Kind::flatten), hitAt({ 0.0f, 0.0f, 0.0f }, { nan, 1.0f, 0.0f }))
              .planeNormal,
            glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST(TerrainStrokeTest, NonFlattenKindsKeepTheTemplatePlane)
{
  terrain::BrushOp op = templateOp(terrain::BrushOp::Kind::smooth);
  op.planePoint = { 5.0f, 6.0f, 7.0f };
  op.planeNormal = { 0.0f, 0.0f, 1.0f };

  TerrainStroke stroke;
  const terrain::BrushOp first = stroke.begin(1, op, hitAt({ 1.0f, 2.0f, 3.0f }, { 1.0f, 0.0f, 0.0f }));
  const std::vector<terrain::BrushOp> due = stroke.moveTo(hitAt({ 4.0f, 2.0f, 3.0f }));

  EXPECT_EQ(first.planePoint, op.planePoint);
  EXPECT_EQ(first.planeNormal, op.planeNormal);
  ASSERT_FALSE(due.empty());
  EXPECT_EQ(due.back().planePoint, op.planePoint);
  EXPECT_EQ(due.back().planeNormal, op.planeNormal);
}

TEST(TerrainStrokeTest, RecordsTheOriginalPointerOfAChunkTouchedRepeatedlyAndTheUnionBox)
{
  terrain::TerrainVolume volume = flatGround();
  const terrain::ChunkCoord groundCoord{ 0, 0, 0 };
  const std::shared_ptr<const terrain::TerrainChunk> originalGround = volume.chunk(groundCoord);
  ASSERT_NE(originalGround, nullptr);

  const terrain::BrushOp first = digOp({ 30.0f, 11.0f, 30.0f });
  const terrain::BrushOp second = digOp({ 33.0f, 11.0f, 30.0f });
  const terrain::SampleBox firstBox = terrain::dirtyBox(volume, first);
  const terrain::SampleBox secondBox = terrain::dirtyBox(volume, second);

  TerrainStroke stroke;
  stroke.begin(9, first, hitAt(first.center));
  applyAndRecord(stroke, volume, first);

  EXPECT_NE(volume.chunk(groundCoord), originalGround);

  applyAndRecord(stroke, volume, second);

  const std::optional<TerrainStroke::Finished> finished = stroke.end();

  ASSERT_TRUE(finished.has_value());
  EXPECT_EQ(finished->strokeId, 9u);
  EXPECT_EQ(finished->box.min, glm::min(firstBox.min, secondBox.min));
  EXPECT_EQ(finished->box.max, glm::max(firstBox.max, secondBox.max));
  EXPECT_EQ(recordsFor(*finished, groundCoord), 1u);

  for (const auto& entry : finished->before)
  {
    if (entry.first == groundCoord)
    {
      EXPECT_EQ(entry.second, originalGround);
    }
  }

  EXPECT_NE(volume.chunk(groundCoord), originalGround);
}

TEST(TerrainStrokeTest, RecordsAnOriginallyAbsentChunkAsNull)
{
  terrain::TerrainVolume volume = flatGround();
  const terrain::ChunkCoord airCoord{ 1, 1, 1 };
  ASSERT_EQ(volume.chunk(airCoord), nullptr);

  terrain::BrushOp raise = digOp({ 100.0f, 100.0f, 100.0f });
  raise.kind = terrain::BrushOp::Kind::add;

  TerrainStroke stroke;
  stroke.begin(3, raise, hitAt(raise.center));
  applyAndRecord(stroke, volume, raise);

  const std::optional<TerrainStroke::Finished> finished = stroke.end();

  ASSERT_TRUE(finished.has_value());
  EXPECT_EQ(recordsFor(*finished, airCoord), 1u);

  for (const auto& entry : finished->before)
  {
    if (entry.first == airCoord)
    {
      EXPECT_EQ(entry.second, nullptr);
    }
  }

  EXPECT_NE(volume.chunk(airCoord), nullptr);
}

TEST(TerrainStrokeTest, EndLifecycle)
{
  TerrainStroke stroke;

  EXPECT_FALSE(stroke.end().has_value());

  stroke.begin(42, templateOp(), hitAt({ 0.0f, 0.0f, 0.0f }));
  const std::optional<TerrainStroke::Finished> finished = stroke.end();

  ASSERT_TRUE(finished.has_value());
  EXPECT_EQ(finished->strokeId, 42u);
  EXPECT_TRUE(finished->before.empty());
  EXPECT_TRUE(finished->box.empty());
  EXPECT_FALSE(stroke.active());
  EXPECT_FALSE(stroke.end().has_value());
  EXPECT_TRUE(stroke.moveTo(hitAt({ 50.0f, 0.0f, 0.0f })).empty());
}

TEST(TerrainStrokeTest, ANewStrokeDoesNotInheritTheLastOnesRecords)
{
  terrain::TerrainVolume volume = flatGround();
  terrain::BrushOp base = templateOp(terrain::BrushOp::Kind::subtract);
  base.radius = 10.0f;

  TerrainStroke stroke;
  applyAndRecord(stroke, volume, stroke.begin(1, base, hitAt({ 30.0f, 11.0f, 30.0f })));
  ASSERT_TRUE(stroke.end().has_value());

  stroke.begin(2, base, hitAt({ 30.0f, 11.0f, 30.0f }));
  const std::optional<TerrainStroke::Finished> second = stroke.end();

  ASSERT_TRUE(second.has_value());
  EXPECT_TRUE(second->before.empty());
  EXPECT_TRUE(second->box.empty());
}

TEST(TerrainStrokeTest, RedistanceBoxesCoverTheInputExactlyWithinTheSideLimit)
{
  const terrain::SampleBox input{ { -50, 3, 10 }, { 350, 43, 60 } };
  const std::vector<terrain::SampleBox> pieces = redistanceBoxes(input);

  ASSERT_GT(pieces.size(), 1u);

  size_t total = 0;

  for (const terrain::SampleBox& piece : pieces)
  {
    EXPECT_FALSE(piece.empty());
    EXPECT_GE(piece.min.x, input.min.x);
    EXPECT_GE(piece.min.y, input.min.y);
    EXPECT_GE(piece.min.z, input.min.z);
    EXPECT_LE(piece.max.x, input.max.x);
    EXPECT_LE(piece.max.y, input.max.y);
    EXPECT_LE(piece.max.z, input.max.z);
    EXPECT_LE(piece.max.x - piece.min.x, 158);
    EXPECT_LE(piece.max.y - piece.min.y, 158);
    EXPECT_LE(piece.max.z - piece.min.z, 158);
    total += volumeOf(piece);
  }

  EXPECT_EQ(total, volumeOf(input));

  for (size_t i = 0; i < pieces.size(); ++i)
  {
    for (size_t j = i + 1; j < pieces.size(); ++j)
    {
      const terrain::SampleBox& a = pieces[i];
      const terrain::SampleBox& b = pieces[j];
      const bool overlap = a.min.x < b.max.x && b.min.x < a.max.x && a.min.y < b.max.y && b.min.y < a.max.y &&
                           a.min.z < b.max.z && b.min.z < a.max.z;
      EXPECT_FALSE(overlap);
    }
  }
}

TEST(TerrainStrokeTest, RedistanceBoxesLeaveASmallBoxAloneAndRefuseEmptyInput)
{
  const terrain::SampleBox small{ { 1, 2, 3 }, { 40, 41, 42 } };
  const std::vector<terrain::SampleBox> pieces = redistanceBoxes(small);

  ASSERT_EQ(pieces.size(), 1u);
  EXPECT_EQ(pieces[0].min, small.min);
  EXPECT_EQ(pieces[0].max, small.max);

  const terrain::SampleBox exact{ { 0, 0, 0 }, { 158, 158, 158 } };
  EXPECT_EQ(redistanceBoxes(exact).size(), 1u);

  EXPECT_TRUE(redistanceBoxes({}).empty());
  EXPECT_TRUE(redistanceBoxes({ { 5, 5, 5 }, { 5, 9, 9 } }).empty());
}

TEST(TerrainStrokeTest, RedistanceAcceptsEveryPieceOfABigBoxButNotTheWholeBox)
{
  terrain::TerrainVolume volume(voxel);
  terrain::commit(volume, terrain::fillFlat(volume, { 160, 158 }, 11.0f, 20.0f, 1));

  terrain::BrushOp dig = templateOp(terrain::BrushOp::Kind::subtract);
  dig.radius = 20.0f;
  dig.strength = 1.0f;
  dig.center = { 100.0f, 11.0f, 100.0f };
  terrain::commit(volume, terrain::applyBrush(volume, dig));

  const terrain::SampleBox big{ { 0, -60, 0 }, { 160, 98, 158 } };

  EXPECT_TRUE(terrain::redistance(volume, big).empty());

  const std::vector<terrain::SampleBox> pieces = redistanceBoxes(big);
  ASSERT_EQ(pieces.size(), 2u);

  size_t changedPieces = 0;

  for (const terrain::SampleBox& piece : pieces)
  {
    if (!terrain::redistance(volume, piece).empty())
    {
      ++changedPieces;
    }
  }

  EXPECT_GT(changedPieces, 0u);
}

TEST(TerrainStrokeTest, RedistanceBoxesForATouchedDiagonalStayProportionalToWhatWasTouched)
{
  const terrain::SampleBox box{ { 0, 0, 0 }, { 4096, 32, 4096 } };
  std::vector<terrain::ChunkCoord> touched;

  for (int i = 0; i < 128; ++i)
  {
    touched.push_back({ i, 0, i });
  }

  const std::vector<terrain::SampleBox> pieces = redistanceBoxes(box, touched);
  const std::vector<terrain::SampleBox> everything = redistanceBoxes(box);

  EXPECT_EQ(everything.size(), 26u * 26u);
  EXPECT_FALSE(pieces.empty());
  EXPECT_LE(pieces.size(), 3u * 26u);

  for (const terrain::ChunkCoord coord : touched)
  {
    const glm::ivec3 origin = terrain::chunkOrigin(coord);

    EXPECT_TRUE(covered(pieces, origin));
    EXPECT_TRUE(covered(pieces, origin + glm::ivec3(terrain::chunkSize - 1)));
  }

  EXPECT_FALSE(covered(pieces, { 4095, 0, 0 }));
  EXPECT_FALSE(covered(pieces, { 0, 0, 4095 }));

  EXPECT_TRUE(redistanceBoxes(box, std::span<const terrain::ChunkCoord>()).empty());
  EXPECT_TRUE(redistanceBoxes({}, touched).empty());

  const std::vector<terrain::ChunkCoord> outside = { { 500, 0, 500 }, { 0, 5, 0 } };
  EXPECT_TRUE(redistanceBoxes(box, outside).empty());
}
