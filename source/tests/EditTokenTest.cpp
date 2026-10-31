#include <gtest/gtest.h>

#include <EditToken.h>
#include <ServerPolicy.h>

#include <algorithm>
#include <array>
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
    return net::generateEditToken(engine);
  }
}

TEST(EditToken, OnlyAnEditServerWithoutATokenNeedsOne)
{
  EXPECT_TRUE(needsGeneratedEditToken(true, false, ""));
  EXPECT_FALSE(needsGeneratedEditToken(true, false, "abc"));
  EXPECT_FALSE(needsGeneratedEditToken(false, false, ""));
  EXPECT_FALSE(needsGeneratedEditToken(false, false, "abc"));
}

TEST(EditToken, AnOpenEditServerGeneratesNoToken)
{
  EXPECT_FALSE(needsGeneratedEditToken(true, true, ""));
  EXPECT_FALSE(needsGeneratedEditToken(true, true, "abc"));
}

TEST(EditToken, OpenEditWarnsOnlyWhenNoTokenWasGiven)
{
  EXPECT_TRUE(needsOpenEditWarning(true, ""));
  EXPECT_FALSE(needsOpenEditWarning(true, "abc"));
  EXPECT_FALSE(needsOpenEditWarning(false, ""));
}

TEST(EditToken, GeneratedTokenIs32LowercaseHexCharacters)
{
  const std::string token = net::generateEditToken();

  EXPECT_EQ(token.length(), 32u);
  EXPECT_TRUE(isLowerHex(token));
}

TEST(EditToken, TwoGeneratedTokensDiffer)
{
  EXPECT_NE(net::generateEditToken(), net::generateEditToken());
}

TEST(EditToken, EveryPositionOfTheTokenCanShowEveryHexDigit)
{
  // Shape and determinism alone pass for a token whose bytes or nibbles carry less entropy than 128 bits;
  // over a few hundred draws each of the 32 positions must have shown all 16 digits.
  std::mt19937 engine(12345);
  std::array<std::array<bool, 16>, 32> seen{};

  for (int draw = 0; draw < 600; ++draw)
  {
    const std::string token = net::generateEditToken(engine);
    ASSERT_EQ(token.length(), 32u);

    for (std::size_t i = 0; i < token.length(); ++i)
    {
      const char c = token[i];
      ASSERT_TRUE(isLowerHex(std::string(1, c))) << "unexpected character at position " << i;
      seen[i][c <= '9' ? c - '0' : c - 'a' + 10] = true;
    }
  }

  for (std::size_t i = 0; i < seen.size(); ++i)
  {
    for (std::size_t digit = 0; digit < 16; ++digit)
    {
      EXPECT_TRUE(seen[i][digit]) << "position " << i << " never showed digit " << digit;
    }
  }
}

TEST(EditToken, SameSeedGivesSameTokenAndDifferentSeedDoesNot)
{
  EXPECT_EQ(seededToken(1), seededToken(1));
  EXPECT_NE(seededToken(1), seededToken(2));
  EXPECT_EQ(seededToken(1).length(), 32u);
  EXPECT_TRUE(isLowerHex(seededToken(1)));
}
