#include <gtest/gtest.h>

#include <EditToken.h>

#include <algorithm>
#include <random>
#include <string>

namespace {
  [[nodiscard]] bool isLowerHex(const std::string& text)
  {
    return std::ranges::all_of(text, [](const char c) {
      return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
  }

  [[nodiscard]] std::string seededToken(const unsigned int seed)
  {
    std::mt19937 engine(seed);
    return generateEditToken(engine);
  }
}

TEST(EditToken, OnlyAnEditServerWithoutATokenNeedsOne)
{
  EXPECT_TRUE(needsGeneratedEditToken(true, ""));
  EXPECT_FALSE(needsGeneratedEditToken(true, "abc"));
  EXPECT_FALSE(needsGeneratedEditToken(false, ""));
  EXPECT_FALSE(needsGeneratedEditToken(false, "abc"));
}

TEST(EditToken, GeneratedTokenIs32LowercaseHexCharacters)
{
  const std::string token = generateEditToken();

  EXPECT_EQ(token.length(), 32u);
  EXPECT_TRUE(isLowerHex(token));
}

TEST(EditToken, TwoGeneratedTokensDiffer)
{
  EXPECT_NE(generateEditToken(), generateEditToken());
}

TEST(EditToken, SameSeedGivesSameTokenAndDifferentSeedDoesNot)
{
  EXPECT_EQ(seededToken(1), seededToken(1));
  EXPECT_NE(seededToken(1), seededToken(2));
  EXPECT_EQ(seededToken(1).length(), 32u);
  EXPECT_TRUE(isLowerHex(seededToken(1)));
}
