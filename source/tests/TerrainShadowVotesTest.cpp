#include <gtest/gtest.h>

#include <TerrainShadowVotes.h>

#include <uuid.h>

namespace {
  uuids::uuid makeUUID(const char* text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const auto terrainA = makeUUID("11111111-1111-4111-8111-111111111111");
  const auto terrainB = makeUUID("22222222-2222-4222-8222-222222222222");
}

TEST(TerrainShadowVotesTest, NoVoteCastsByDefault)
{
  const TerrainShadowVotes votes;

  EXPECT_TRUE(votes.castsShadows(terrainA));
}

TEST(TerrainShadowVotesTest, SingleFalseVoteDoesNotCast)
{
  TerrainShadowVotes votes;
  votes.vote(terrainA, false);
  votes.vote(terrainB, true);

  EXPECT_FALSE(votes.castsShadows(terrainA));
  EXPECT_TRUE(votes.castsShadows(terrainB));
}

TEST(TerrainShadowVotesTest, AnyTrueVoteCastsInEitherOrder)
{
  TerrainShadowVotes votes;
  votes.vote(terrainA, false);
  votes.vote(terrainA, true);
  votes.vote(terrainB, true);
  votes.vote(terrainB, false);

  EXPECT_TRUE(votes.castsShadows(terrainA));
  EXPECT_TRUE(votes.castsShadows(terrainB));
}

TEST(TerrainShadowVotesTest, TerrainsAreIndependent)
{
  TerrainShadowVotes votes;
  votes.vote(terrainA, false);
  votes.vote(terrainB, true);
  votes.vote(terrainB, false);

  EXPECT_FALSE(votes.castsShadows(terrainA));
  EXPECT_TRUE(votes.castsShadows(terrainB));
}

TEST(TerrainShadowVotesTest, ClearRestoresTheDefault)
{
  TerrainShadowVotes votes;
  votes.vote(terrainA, false);
  ASSERT_FALSE(votes.castsShadows(terrainA));

  votes.clear();

  EXPECT_TRUE(votes.castsShadows(terrainA));
}

TEST(TerrainShadowVotesTest, NilUuidIsAnOrdinaryKey)
{
  TerrainShadowVotes votes;
  votes.vote(uuids::uuid{}, false);

  EXPECT_FALSE(votes.castsShadows(uuids::uuid{}));
  EXPECT_TRUE(votes.castsShadows(terrainA));
}
