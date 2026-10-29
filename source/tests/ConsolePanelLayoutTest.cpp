#include <ConsolePanelLayout.h>
#include <gtest/gtest.h>
#include <vector>

namespace {
  // Lays items out left to right the way ConsolePanel does and returns how many land on each line.
  std::vector<int> lineCounts(const std::vector<float>& widths, const float gap, const float avail)
  {
    std::vector<int> counts;
    float used = 0.0f;
    for (const float width : widths)
    {
      if (counts.empty() || !fitsOnLine(used, gap, width, avail))
      {
        counts.push_back(1);
        used = width;
      }
      else
      {
        ++counts.back();
        used += gap + width;
      }
    }
    return counts;
  }
}

TEST(ConsolePanelLayoutTest, FirstItemOnALineAlwaysFits)
{
  EXPECT_TRUE(fitsOnLine(0.0f, 16.0f, 900.0f, 100.0f));
}

TEST(ConsolePanelLayoutTest, ItemFitsWhenGapAndWidthStayWithinAvailable)
{
  EXPECT_TRUE(fitsOnLine(100.0f, 16.0f, 84.0f, 200.0f));
  EXPECT_FALSE(fitsOnLine(100.0f, 16.0f, 85.0f, 200.0f));
}

TEST(ConsolePanelLayoutTest, NarrowPanelWrapsInsteadOfRunningPastTheEdge)
{
  const std::vector<float> widths(5, 100.0f);
  const auto counts = lineCounts(widths, 16.0f, 400.0f);

  ASSERT_EQ(counts.size(), 2u);
  EXPECT_EQ(counts[0], 3);
  EXPECT_EQ(counts[1], 2);
}

TEST(ConsolePanelLayoutTest, WidePanelKeepsEverythingOnOneLine)
{
  const std::vector<float> widths(5, 100.0f);
  EXPECT_EQ(lineCounts(widths, 16.0f, 2000.0f).size(), 1u);
}

TEST(ConsolePanelLayoutTest, OversizedItemGetsItsOwnLine)
{
  const auto counts = lineCounts({ 50.0f, 500.0f, 50.0f }, 16.0f, 400.0f);
  EXPECT_EQ(counts.size(), 3u);
}
