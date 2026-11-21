#include <gtest/gtest.h>

#include <TerrainSideVotes.h>
#include <terrain/TerrainTypes.h>

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

  EXPECT_EQ(votes.openSides(terrainA), 0);
}

TEST(TerrainSideVotesTest, ASingleVoteSetsItsMask)
{
  TerrainSideVotes votes;
  votes.vote(terrainA, terrain::minX | terrain::maxZ);

  EXPECT_EQ(votes.openSides(terrainA), terrain::minX | terrain::maxZ);
}

TEST(TerrainSideVotesTest, EachSideIsAnAndAcrossTheVotes)
{
  TerrainSideVotes votes;
  votes.vote(terrainA, terrain::minX | terrain::maxX | terrain::maxZ);
  votes.vote(terrainA, terrain::maxX | terrain::maxZ | terrain::minZ);

  EXPECT_EQ(votes.openSides(terrainA), terrain::maxX | terrain::maxZ);

  // Order does not matter, and a vote for no sides closes everything.
  votes.vote(terrainB, terrain::maxX | terrain::maxZ | terrain::minZ);
  votes.vote(terrainB, terrain::minX | terrain::maxX | terrain::maxZ);
  EXPECT_EQ(votes.openSides(terrainB), terrain::maxX | terrain::maxZ);

  votes.vote(terrainA, 0);
  EXPECT_EQ(votes.openSides(terrainA), 0);
}

TEST(TerrainSideVotesTest, TerrainsAreIndependent)
{
  TerrainSideVotes votes;
  votes.vote(terrainA, terrain::allSides);
  votes.vote(terrainB, 0);

  EXPECT_EQ(votes.openSides(terrainA), terrain::allSides);
  EXPECT_EQ(votes.openSides(terrainB), 0);
}

TEST(TerrainSideVotesTest, ClearRestoresTheClosedDefault)
{
  TerrainSideVotes votes;
  votes.vote(terrainA, terrain::allSides);
  ASSERT_EQ(votes.openSides(terrainA), terrain::allSides);

  votes.clear();

  EXPECT_EQ(votes.openSides(terrainA), 0);
}
