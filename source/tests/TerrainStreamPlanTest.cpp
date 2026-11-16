#include <gtest/gtest.h>

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainCodec.h"
#include "terrain/TerrainStreamPlan.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <set>
#include <utility>
#include <vector>

namespace {
  using terrain::ChunkCoord;

  std::shared_ptr<const terrain::TerrainChunk> solid(const uint8_t material = 1)
  {
    return terrain::TerrainChunk::uniform(-terrain::airDistance, material);
  }

  std::shared_ptr<const terrain::TerrainChunk> noise(const uint32_t seed)
  {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> distance(-terrain::airDistance, terrain::airDistance);
    std::uniform_int_distribution<int> material(0, 254);
    std::vector<int16_t> distances(terrain::samplesPerChunk);
    std::vector<uint8_t> materials(terrain::samplesPerChunk);

    for (int i = 0; i < terrain::samplesPerChunk; ++i)
    {
      distances[static_cast<size_t>(i)] = static_cast<int16_t>(distance(rng));
      materials[static_cast<size_t>(i)] = static_cast<uint8_t>(material(rng));
    }

    return terrain::TerrainChunk::dense(std::move(distances), std::move(materials));
  }

  std::vector<ChunkCoord> drain(terrain::TerrainStreamPlan& plan, const terrain::TerrainVolume& volume,
                                const size_t budget = terrain::defaultStreamBudgetBytes)
  {
    std::vector<ChunkCoord> order;

    while (!plan.done())
    {
      for (const auto& entry : plan.next(volume, budget))
      {
        order.push_back(entry.coord);
      }
    }

    return order;
  }

  size_t framedSize(const std::vector<terrain::StreamedChunk>& batch)
  {
    size_t total = terrain::streamMessageOverheadBytes;

    for (const auto& entry : batch)
    {
      total += terrain::streamEntryOverheadBytes + entry.encoded.size();
    }

    return total;
  }

  // The center of chunk 0 is sample 16, so this focus sits in its middle.
  constexpr glm::vec3 originFocus{ 16.0f, 16.0f, 16.0f };
}

TEST(TerrainStreamPlanTest, ChunksComeOutNearestFirstAcrossCalls)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 5, 0, 0 }, solid());
  volume.setChunk({ 1, 0, 0 }, solid());
  volume.setChunk({ 0, 0, 0 }, solid());
  volume.setChunk({ 3, 0, 0 }, solid());

  terrain::TerrainStreamPlan plan(volume, originFocus);

  // The budget fits one tiny chunk, so each call takes exactly one.
  const size_t oneChunk = terrain::streamMessageOverheadBytes + terrain::streamEntryOverheadBytes +
                          terrain::encodeChunk(*solid()).size();
  std::vector<ChunkCoord> order;

  while (!plan.done())
  {
    const auto batch = plan.next(volume, oneChunk);
    ASSERT_EQ(batch.size(), 1u);
    order.push_back(batch.front().coord);
  }

  const std::vector<ChunkCoord> expected{ { 0, 0, 0 }, { 1, 0, 0 }, { 3, 0, 0 }, { 5, 0, 0 } };
  EXPECT_EQ(order, expected);
}

TEST(TerrainStreamPlanTest, TiesComeOutInZYXOrder)
{
  terrain::TerrainVolume volume;

  for (const ChunkCoord coord : { ChunkCoord{ 1, 0, 0 }, ChunkCoord{ 0, 1, 0 }, ChunkCoord{ 0, 0, 1 },
                                  ChunkCoord{ -1, 0, 0 } })
  {
    volume.setChunk(coord, solid());
  }

  // The focus is the origin chunk's center, so the four neighbors are equally far away.
  terrain::TerrainStreamPlan plan(volume, originFocus);

  const std::vector<ChunkCoord> expected{ { -1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
  EXPECT_EQ(drain(plan, volume), expected);
}

TEST(TerrainStreamPlanTest, OppositeFocusReversesTheOrder)
{
  terrain::TerrainVolume volume;

  for (int32_t x = 0; x < 6; ++x)
  {
    volume.setChunk({ x, 0, 0 }, solid());
  }

  terrain::TerrainStreamPlan nearPlan(volume, originFocus);
  terrain::TerrainStreamPlan farPlan(volume, { 5.0f * terrain::chunkSize + 16.0f, 16.0f, 16.0f });

  const std::vector<ChunkCoord> nearOrder = drain(nearPlan, volume);
  const std::vector<ChunkCoord> farOrder = drain(farPlan, volume);

  ASSERT_EQ(nearOrder.size(), 6u);
  EXPECT_EQ(nearOrder.front().x, 0);
  EXPECT_EQ(farOrder.front().x, 5);
  EXPECT_EQ(std::vector<ChunkCoord>(farOrder.rbegin(), farOrder.rend()), nearOrder);
}

TEST(TerrainStreamPlanTest, BatchesStayWithinBudgetAndCoverEveryChunkOnce)
{
  terrain::TerrainVolume volume;
  constexpr int count = 9;

  for (int32_t x = 0; x < count; ++x)
  {
    volume.setChunk({ x, 0, 0 }, noise(static_cast<uint32_t>(x)));
  }

  const size_t budget = terrain::defaultStreamBudgetBytes;
  terrain::TerrainStreamPlan plan(volume, originFocus);
  ASSERT_EQ(plan.pending(), static_cast<size_t>(count));

  std::set<int32_t> seen;
  size_t taken = 0;
  size_t batches = 0;

  while (!plan.done())
  {
    const auto batch = plan.next(volume, budget);
    ASSERT_FALSE(batch.empty());
    EXPECT_LE(framedSize(batch), budget);

    for (const auto& entry : batch)
    {
      EXPECT_TRUE(seen.insert(entry.coord.x).second);
      EXPECT_EQ(entry.encoded, terrain::encodeChunk(*entry.chunk));
      ++taken;
    }

    ++batches;
    EXPECT_EQ(plan.done(), taken == static_cast<size_t>(count));
  }

  EXPECT_EQ(taken, static_cast<size_t>(count));
  EXPECT_GT(batches, 1u);
}

TEST(TerrainStreamPlanTest, BudgetSmallerThanOneChunkStillYieldsOneChunkPerCall)
{
  terrain::TerrainVolume volume;

  for (int32_t x = 0; x < 3; ++x)
  {
    volume.setChunk({ x, 0, 0 }, noise(static_cast<uint32_t>(x)));
  }

  terrain::TerrainStreamPlan tight(volume, originFocus);
  EXPECT_EQ(tight.next(volume, 1).size(), 1u);
  EXPECT_EQ(tight.pending(), 2u);

  // Positive control: a budget that fits the first two framed chunks yields two.
  const size_t entry = terrain::streamEntryOverheadBytes + terrain::encodeChunk(*noise(0)).size();
  const size_t roomyBudget = terrain::streamMessageOverheadBytes + 2 * entry;
  const size_t expectedRoomy = std::min<size_t>(3, (roomyBudget - terrain::streamMessageOverheadBytes) / entry);

  terrain::TerrainStreamPlan roomy(volume, originFocus);
  EXPECT_EQ(roomy.next(volume, roomyBudget).size(), expectedRoomy);
  EXPECT_GT(expectedRoomy, 1u);
}

TEST(TerrainStreamPlanTest, ALaterChunkThatDoesNotFitEndsTheBatchWithoutSkippingAhead)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid());
  volume.setChunk({ 1, 0, 0 }, noise(1));
  volume.setChunk({ 2, 0, 0 }, solid());

  terrain::TerrainStreamPlan plan(volume, originFocus);
  const size_t smallEntry = terrain::streamEntryOverheadBytes + terrain::encodeChunk(*solid()).size();
  const size_t budget = terrain::streamMessageOverheadBytes + 2 * smallEntry;

  const auto first = plan.next(volume, budget);
  ASSERT_EQ(first.size(), 1u);
  EXPECT_EQ(first.front().coord, (ChunkCoord{ 0, 0, 0 }));
  EXPECT_EQ(plan.pending(), 2u);

  // Positive control: with room for everything the farther small chunk follows the large one in order.
  terrain::TerrainStreamPlan roomy(volume, originFocus);
  const std::vector<ChunkCoord> expected{ { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 } };
  EXPECT_EQ(drain(roomy, volume), expected);
}

TEST(TerrainStreamPlanTest, RequeueSendsAnEditedChunkAgain)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid(1));

  terrain::TerrainStreamPlan plan(volume, originFocus);
  const auto first = plan.next(volume);
  ASSERT_EQ(first.size(), 1u);
  EXPECT_TRUE(plan.done());

  volume.setChunk({ 0, 0, 0 }, solid(2));
  plan.requeue({ 0, 0, 0 });
  EXPECT_FALSE(plan.done());

  const auto second = plan.next(volume);
  ASSERT_EQ(second.size(), 1u);
  EXPECT_EQ(second.front().chunk->hash(), solid(2)->hash());
  EXPECT_NE(second.front().chunk->hash(), first.front().chunk->hash());
}

TEST(TerrainStreamPlanTest, RequeueOfAPendingCoordinateDoesNotDuplicateIt)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid());
  volume.setChunk({ 1, 0, 0 }, solid());

  terrain::TerrainStreamPlan plan(volume, originFocus);
  plan.requeue({ 1, 0, 0 });
  plan.requeue({ 1, 0, 0 });
  EXPECT_EQ(plan.pending(), 2u);

  plan.requeue({ 2, 0, 0 });
  EXPECT_EQ(plan.pending(), 3u);

  plan.requeue({ terrain::maxChunkCoord + 1, 0, 0 });
  EXPECT_EQ(plan.pending(), 3u);
}

TEST(TerrainStreamPlanTest, AChunkErasedFromTheVolumeComesOutAsAir)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid());

  terrain::TerrainStreamPlan plan(volume, originFocus);
  const auto sent = plan.next(volume);
  ASSERT_EQ(sent.size(), 1u);
  EXPECT_FALSE(sent.front().chunk->isAir());

  volume.setChunk({ 0, 0, 0 }, nullptr);
  plan.requeue({ 0, 0, 0 });

  const auto batch = plan.next(volume);
  ASSERT_EQ(batch.size(), 1u);
  ASSERT_NE(batch.front().chunk, nullptr);
  EXPECT_TRUE(batch.front().chunk->isAir());
  EXPECT_EQ(batch.front().encoded, terrain::encodeChunk(*batch.front().chunk));
}

TEST(TerrainStreamPlanTest, AChunkEditedWhilePendingIsSentOnceAsEdited)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid(1));

  terrain::TerrainStreamPlan plan(volume, originFocus);
  volume.setChunk({ 0, 0, 0 }, solid(2));
  plan.requeue({ 0, 0, 0 });
  EXPECT_EQ(plan.pending(), 1u);

  const auto batch = plan.next(volume);
  ASSERT_EQ(batch.size(), 1u);
  EXPECT_EQ(batch.front().chunk->hash(), solid(2)->hash());
  EXPECT_TRUE(plan.done());
}

TEST(TerrainStreamPlanTest, EmptyVolumeIsDoneImmediately)
{
  const terrain::TerrainVolume volume;
  terrain::TerrainStreamPlan plan(volume, originFocus);

  EXPECT_TRUE(plan.done());
  EXPECT_EQ(plan.pending(), 0u);
  EXPECT_TRUE(plan.next(volume).empty());
}

TEST(TerrainStreamPlanTest, FarCoordinatesOrderWithoutOverflow)
{
  terrain::TerrainVolume volume;
  const int32_t edge = terrain::maxChunkCoord;
  volume.setChunk({ edge, edge, edge }, solid());
  volume.setChunk({ edge - 1, edge, edge }, solid());
  volume.setChunk({ -edge, -edge, -edge }, solid());

  // A float holds only multiples of 64 here, so the focus sits a few chunks past the far corner exactly.
  const float corner = static_cast<float>(edge) * terrain::chunkSize + 64.0f;
  terrain::TerrainStreamPlan plan(volume, glm::vec3(corner));

  const std::vector<ChunkCoord> expected{ { edge, edge, edge }, { edge - 1, edge, edge }, { -edge, -edge, -edge } };
  EXPECT_EQ(drain(plan, volume), expected);
}

TEST(TerrainStreamPlanTest, BudgetBoundaryIsInclusive)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid());
  volume.setChunk({ 1, 0, 0 }, solid());

  const size_t exact = terrain::streamMessageOverheadBytes +
                       2 * (terrain::streamEntryOverheadBytes + terrain::encodeChunk(*solid()).size());

  terrain::TerrainStreamPlan fits(volume, originFocus);
  EXPECT_EQ(fits.next(volume, exact).size(), 2u);

  terrain::TerrainStreamPlan shortByOne(volume, originFocus);
  EXPECT_EQ(shortByOne.next(volume, exact - 1).size(), 1u);
}

TEST(TerrainStreamPlanTest, RequeueAtMaxChunkCoordIsAccepted)
{
  const terrain::TerrainVolume volume;
  terrain::TerrainStreamPlan plan(volume, originFocus);

  plan.requeue({ terrain::maxChunkCoord, -terrain::maxChunkCoord, terrain::maxChunkCoord });
  EXPECT_EQ(plan.pending(), 1u);

  plan.requeue({ terrain::maxChunkCoord + 1, 0, 0 });
  EXPECT_EQ(plan.pending(), 1u);
}

TEST(TerrainStreamPlanTest, RequeuedNearChunkComesOutBeforePendingFarChunks)
{
  terrain::TerrainVolume volume;

  for (int32_t x = 0; x < 5; ++x)
  {
    volume.setChunk({ x, 0, 0 }, solid());
  }

  terrain::TerrainStreamPlan plan(volume, originFocus);
  const size_t oneChunk = terrain::streamMessageOverheadBytes + terrain::streamEntryOverheadBytes +
                          terrain::encodeChunk(*solid()).size();

  ASSERT_EQ(plan.next(volume, oneChunk).front().coord.x, 0);

  plan.requeue({ 0, 0, 0 });
  const auto batch = plan.next(volume, oneChunk);
  ASSERT_EQ(batch.size(), 1u);
  EXPECT_EQ(batch.front().coord.x, 0);
  EXPECT_EQ(plan.next(volume, oneChunk).front().coord.x, 1);
}

TEST(TerrainStreamPlanTest, NonFiniteFocusStillQueuesEveryChunk)
{
  terrain::TerrainVolume volume;

  for (int32_t x = 0; x < 5; ++x)
  {
    volume.setChunk({ x, 0, 0 }, solid());
  }

  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();

  terrain::TerrainStreamPlan nanPlan(volume, { nan, 16.0f, nan });
  EXPECT_EQ(nanPlan.pending(), 5u);
  EXPECT_EQ(drain(nanPlan, volume).size(), 5u);

  terrain::TerrainStreamPlan infPlan(volume, { inf, -inf, 16.0f });
  EXPECT_EQ(infPlan.pending(), 5u);
}

TEST(TerrainStreamPlanTest, AnEntryCapEndsABatchBeforeTheBudgetDoes)
{
  terrain::TerrainVolume volume;

  for (int32_t x = 0; x < 5; ++x)
  {
    volume.setChunk({ x, 0, 0 }, solid());
  }

  terrain::TerrainStreamPlan capped(volume, originFocus);
  EXPECT_EQ(capped.next(volume, terrain::defaultStreamBudgetBytes, 2).size(), 2u);
  EXPECT_EQ(capped.pending(), 3u);

  // Positive control: with no cap the same budget takes all five.
  terrain::TerrainStreamPlan uncapped(volume, originFocus);
  EXPECT_EQ(uncapped.next(volume, terrain::defaultStreamBudgetBytes).size(), 5u);
}

TEST(TerrainStreamPlanTest, AnEntryCapOfZeroStillTakesOneChunk)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid());
  volume.setChunk({ 1, 0, 0 }, solid());

  terrain::TerrainStreamPlan plan(volume, originFocus);
  EXPECT_EQ(plan.next(volume, terrain::defaultStreamBudgetBytes, 0).size(), 1u);
  EXPECT_EQ(plan.pending(), 1u);
}

TEST(TerrainStreamPlanTest, WithoutForceFirstAFirstChunkThatDoesNotFitStaysQueued)
{
  terrain::TerrainVolume volume(1.0f);
  volume.setChunk({ 0, 0, 0 }, noise(1));

  terrain::TerrainStreamPlan plan(volume, originFocus);

  EXPECT_TRUE(plan.next(volume, 1, std::numeric_limits<size_t>::max(), false).empty());
  EXPECT_EQ(plan.pending(), 1u);

  // Positive controls: forcing the first chunk takes it, and so does a budget it fits in.
  terrain::TerrainStreamPlan roomy(volume, originFocus);
  EXPECT_EQ(roomy.next(volume, terrain::defaultStreamBudgetBytes, std::numeric_limits<size_t>::max(), false).size(), 1u);
  EXPECT_EQ(plan.next(volume, 1).size(), 1u);
  EXPECT_TRUE(plan.done());
}
