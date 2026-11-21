#include <gtest/gtest.h>

#include <TerrainSideVotes.h>

#include <uuid.h>

namespace {
  uuids::uuid makeUUID(const char* text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const auto terrainA = makeUUID("11111111-1111-4111-8111-111111111111");
  const auto terrainB = makeUUID("22222222-2222-4222-8222-222222222222");
}

TEST(TerrainSideVotesTest, NoVoteIsClosed)
{
  const TerrainSideVotes votes;

  EXPECT_FALSE(votes.opensSides(terrainA));
}

TEST(TerrainSideVotesTest, AllTrueVotesOpenTheSides)
{
  TerrainSideVotes votes;
  votes.vote(terrainA, true);
  votes.vote(terrainA, true);

  EXPECT_TRUE(votes.opensSides(terrainA));
}

TEST(TerrainSideVotesTest, AnyFalseVoteClosesThemInEitherOrder)
{
  TerrainSideVotes votes;
  votes.vote(terrainA, true);
  votes.vote(terrainA, false);
  votes.vote(terrainB, false);
  votes.vote(terrainB, true);

  EXPECT_FALSE(votes.opensSides(terrainA));
  EXPECT_FALSE(votes.opensSides(terrainB));
}

TEST(TerrainSideVotesTest, TerrainsAreIndependent)
{
  TerrainSideVotes votes;
  votes.vote(terrainA, true);
  votes.vote(terrainB, false);

  EXPECT_TRUE(votes.opensSides(terrainA));
  EXPECT_FALSE(votes.opensSides(terrainB));
}

TEST(TerrainSideVotesTest, ClearRestoresTheClosedDefault)
{
  TerrainSideVotes votes;
  votes.vote(terrainA, true);
  ASSERT_TRUE(votes.opensSides(terrainA));

  votes.clear();

  EXPECT_FALSE(votes.opensSides(terrainA));
}
