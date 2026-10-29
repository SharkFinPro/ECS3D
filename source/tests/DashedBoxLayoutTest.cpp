#include <DashedBoxLayout.h>
#include <gtest/gtest.h>
#include <string>
#include <string_view>

namespace
{
  // A fixed-pitch font: 10 units per character.
  float measure(const std::string_view s)
  {
    return static_cast<float>(s.size()) * 10.0f;
  }
}

TEST(DashedBoxLayoutTest, ShortTextStaysOnOneLine)
{
  const auto lines = wrapTextLines("Stop the scene", 500.0f, measure);

  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "Stop the scene");
}

TEST(DashedBoxLayoutTest, LongHintWrapsWithinWidth)
{
  const auto lines = wrapTextLines("The active scene can't be deleted. Load another scene first.", 200.0f, measure);

  EXPECT_GT(lines.size(), 1u);
  for (const auto& line : lines)
  {
    EXPECT_LE(measure(line), 200.0f) << line;
  }
}

TEST(DashedBoxLayoutTest, WrappingKeepsEveryWordInOrder)
{
  const std::string text = "The active scene can't be deleted. Load another scene first.";
  const auto lines = wrapTextLines(text, 200.0f, measure);

  std::string rejoined;
  for (const auto& line : lines)
  {
    rejoined += (rejoined.empty() ? "" : " ") + line;
  }

  EXPECT_EQ(rejoined, text);
}

TEST(DashedBoxLayoutTest, WordWiderThanTheBoxIsSplit)
{
  const std::string uuid = "123e4567-e89b-12d3-a456-426614174000";
  const auto lines = wrapTextLines(uuid, 150.0f, measure);

  EXPECT_GT(lines.size(), 1u);
  std::string rejoined;
  for (const auto& line : lines)
  {
    EXPECT_LE(measure(line), 150.0f);
    rejoined += line;
  }

  EXPECT_EQ(rejoined, uuid);
}

TEST(DashedBoxLayoutTest, BoxGrowsOnlyWhenTextNeedsIt)
{
  EXPECT_FLOAT_EQ(dashedBoxHeight(1, 16.0f, 10.0f, 60.0f), 60.0f);
  EXPECT_FLOAT_EQ(dashedBoxHeight(4, 16.0f, 10.0f, 60.0f), 84.0f);
}

TEST(DashedBoxLayoutTest, EmptyTextHasNoLines)
{
  EXPECT_TRUE(wrapTextLines("", 100.0f, measure).empty());
}

TEST(DashedBoxLayoutTest, ExtraSpacesCollapseWithoutLosingCharacters)
{
  const auto lines = wrapTextLines("  ab   cd  ", 500.0f, measure);

  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "ab cd");
}

TEST(DashedBoxLayoutTest, ALineExactlyAsWideAsTheLimitStaysTogether)
{
  const auto lines = wrapTextLines("ab cd", 50.0f, measure);

  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "ab cd");
}

TEST(DashedBoxLayoutTest, ZeroOrNegativeWidthTerminatesWithOneCharacterPerLine)
{
  for (const float width : { 0.0f, -5.0f })
  {
    const auto lines = wrapTextLines("ab cd", width, measure);

    ASSERT_EQ(lines.size(), 4u);
    for (const auto& line : lines)
    {
      EXPECT_EQ(line.size(), 1u);
    }
  }
}

TEST(DashedBoxLayoutTest, ALongWordAfterOthersFlushesThemThenSplitsAndLaterWordsJoinTheRemainder)
{
  const auto lines = wrapTextLines("hi abcdefgh y", 50.0f, measure);

  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "hi");
  EXPECT_EQ(lines[1], "abcde");
  EXPECT_EQ(lines[2], "fgh y");
}
