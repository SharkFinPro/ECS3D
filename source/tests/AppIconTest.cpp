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

  // Returns an empty string when the buffer is a well-formed ICO, otherwise the problem. Fills `sizes`
  // with each entry's pixel width (a stored 0 means 256).
  std::string validateIco(const std::vector<std::uint8_t>& bytes, std::set<int>& sizes)
  {
    constexpr std::size_t headerSize = 6;
    constexpr std::size_t entrySize = 16;

    if (bytes.size() < headerSize)
    {
      return "shorter than the ICONDIR header";
    }
    if (readLe(bytes, 0, 2) != 0 || readLe(bytes, 2, 2) != 1)
    {
      return "bad reserved or type field";
    }

    const std::size_t count = readLe(bytes, 4, 2);
    if (count == 0)
    {
      return "no entries";
    }
    if (bytes.size() < headerSize + count * entrySize)
    {
      return "entry table is cut off";
    }

    for (std::size_t i = 0; i < count; ++i)
    {
      const std::size_t entry = headerSize + i * entrySize;
      const std::uint32_t imageSize = readLe(bytes, entry + 8, 4);
      const std::uint32_t offset = readLe(bytes, entry + 12, 4);
      if (imageSize == 0 || static_cast<std::uint64_t>(offset) + imageSize > bytes.size())
      {
        return "entry " + std::to_string(i) + " lies outside the file";
      }

      const int width = bytes[entry] == 0 ? 256 : bytes[entry];
      const int height = bytes[entry + 1] == 0 ? 256 : bytes[entry + 1];
      if (width != height)
      {
        return "entry " + std::to_string(i) + " is not square";
      }
      sizes.insert(width);
    }

    return "";
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
