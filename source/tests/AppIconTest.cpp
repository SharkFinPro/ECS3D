#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace
{
  const std::filesystem::path iconDir = std::filesystem::path(ECS3D_SOURCE_DIR) / "source" / "apps" / "icons";

  std::vector<std::uint8_t> readFile(const std::filesystem::path& path)
  {
    std::ifstream file(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
  }

  std::uint32_t readLe(const std::vector<std::uint8_t>& bytes, const std::size_t at, const int count)
  {
    std::uint32_t value = 0;
    for (int i = 0; i < count; ++i)
    {
      value |= static_cast<std::uint32_t>(bytes[at + i]) << (8 * i);
    }
    return value;
  }

  constexpr std::size_t icoHeaderSize = 6;
  constexpr std::size_t icoEntrySize = 16;

  // A stored 0 means 256.
  int icoDimension(const std::uint8_t stored)
  {
    return stored == 0 ? 256 : stored;
  }

  // Returns an empty string when the header is sound, otherwise the problem. Sets `count` to the entry count.
  std::string validateIcoHeader(const std::vector<std::uint8_t>& bytes, std::size_t& count)
  {
    if (bytes.size() < icoHeaderSize)
    {
      return "shorter than the ICONDIR header";
    }
    if (readLe(bytes, 0, 2) != 0 || readLe(bytes, 2, 2) != 1)
    {
      return "bad reserved or type field";
    }

    count = readLe(bytes, 4, 2);
    if (count == 0)
    {
      return "no entries";
    }
    if (bytes.size() < icoHeaderSize + count * icoEntrySize)
    {
      return "entry table is cut off";
    }
    return "";
  }

  // Returns an empty string when the entry is sound, otherwise the problem. Adds its pixel width to `sizes`.
  std::string validateIcoEntry(const std::vector<std::uint8_t>& bytes, const std::size_t index, std::set<int>& sizes)
  {
    const std::size_t entry = icoHeaderSize + index * icoEntrySize;
    const std::uint32_t imageSize = readLe(bytes, entry + 8, 4);
    const std::uint32_t offset = readLe(bytes, entry + 12, 4);
    if (imageSize == 0 || static_cast<std::uint64_t>(offset) + imageSize > bytes.size())
    {
      return "entry " + std::to_string(index) + " lies outside the file";
    }

    const int width = icoDimension(bytes[entry]);
    if (width != icoDimension(bytes[entry + 1]))
    {
      return "entry " + std::to_string(index) + " is not square";
    }
    sizes.insert(width);
    return "";
  }

  // Returns an empty string when the buffer is a well-formed ICO, otherwise the problem. Fills `sizes`
  // with each entry's pixel width.
  std::string validateIco(const std::vector<std::uint8_t>& bytes, std::set<int>& sizes)
  {
    std::size_t count = 0;
    std::string problem = validateIcoHeader(bytes, count);
    for (std::size_t i = 0; problem.empty() && i < count; ++i)
    {
      problem = validateIcoEntry(bytes, i, sizes);
    }
    return problem;
  }

  void expectValidIcon(const std::string& fileName)
  {
    const auto bytes = readFile(iconDir / fileName);
    ASSERT_FALSE(bytes.empty()) << fileName << " is missing or empty";

    std::set<int> sizes;
    EXPECT_EQ(validateIco(bytes, sizes), "") << fileName;
    for (const int required : { 16, 24, 32, 48, 256 })
    {
      EXPECT_TRUE(sizes.count(required)) << fileName << " has no " << required << " px entry";
    }
  }
}

TEST(AppIcon, EditorIconIsAValidIco)
{
  expectValidIcon("editor.ico");
}

TEST(AppIcon, ClientIconIsAValidIco)
{
  expectValidIcon("client.ico");
}

TEST(AppIcon, ValidationRejectsDamagedBuffers)
{
  const auto good = readFile(iconDir / "editor.ico");
  std::set<int> sizes;
  ASSERT_EQ(validateIco(good, sizes), "");

  auto truncated = good;
  truncated.resize(good.size() / 2);
  sizes.clear();
  EXPECT_NE(validateIco(truncated, sizes), "");

  auto wrongType = good;
  wrongType[2] = 2;
  sizes.clear();
  EXPECT_NE(validateIco(wrongType, sizes), "");

  sizes.clear();
  EXPECT_NE(validateIco({}, sizes), "");
}
