#include <gtest/gtest.h>

#include <Log.h>
#include <RingBufferSink.h>
#include <SharedAssetLoads.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace {
  class SharedAssetLoadsTest : public testing::Test {
  protected:
    void SetUp() override
    {
      m_sink = std::make_shared<RingBufferSink>(100);
      Log::addSink(m_sink);
    }

    void TearDown() override
    {
      Log::removeSink(m_sink);
    }

    std::shared_ptr<RingBufferSink> m_sink;
    SharedAssetLoads<int> m_loads;
  };
}

TEST_F(SharedAssetLoadsTest, LoadsOncePerPathAndSharesTheResult)
{
  int calls = 0;
  const auto load = [&calls] {
    ++calls;
    return std::make_shared<int>(7);
  };

  const auto first = m_loads.get("a.glb", load);
  const auto second = m_loads.get("a.glb", load);

  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first, second);
  EXPECT_EQ(calls, 1);
  EXPECT_TRUE(m_sink->snapshot().empty());
}

TEST_F(SharedAssetLoadsTest, ThrowingLoaderYieldsNullAndIsNotRetried)
{
  int calls = 0;
  const auto load = [&calls]() -> std::shared_ptr<int> {
    ++calls;
    throw std::runtime_error("Assimp Error: missing");
  };

  EXPECT_EQ(m_loads.get("missing.glb", load), nullptr);
  EXPECT_EQ(m_loads.get("missing.glb", load), nullptr);
  EXPECT_EQ(m_loads.get("missing.glb", load), nullptr);
  EXPECT_EQ(calls, 1);
}

TEST_F(SharedAssetLoadsTest, FailureIsLoggedOnceWithPathAndReason)
{
  const auto load = []() -> std::shared_ptr<int> { throw std::runtime_error("Assimp Error: missing"); };

  static_cast<void>(m_loads.get("missing.glb", load));
  static_cast<void>(m_loads.get("missing.glb", load));

  const auto entries = m_sink->snapshot();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].level, LogLevel::error);
  EXPECT_NE(entries[0].message.find("missing.glb"), std::string::npos);
  EXPECT_NE(entries[0].message.find("Assimp Error: missing"), std::string::npos);
}

TEST_F(SharedAssetLoadsTest, NullFromLoaderCountsAsAFailureAndIsRemembered)
{
  int calls = 0;
  const auto load = [&calls] {
    ++calls;
    return std::shared_ptr<int>();
  };

  EXPECT_EQ(m_loads.get("empty.glb", load), nullptr);
  EXPECT_EQ(m_loads.get("empty.glb", load), nullptr);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(m_sink->snapshot().size(), 1u);
}

TEST_F(SharedAssetLoadsTest, OnePathFailingDoesNotAffectAnother)
{
  const auto bad = []() -> std::shared_ptr<int> { throw std::runtime_error("nope"); };
  const auto good = [] { return std::make_shared<int>(3); };

  EXPECT_EQ(m_loads.get("bad.glb", bad), nullptr);

  const auto loaded = m_loads.get("good.glb", good);
  ASSERT_NE(loaded, nullptr);
  EXPECT_EQ(*loaded, 3);
  EXPECT_EQ(m_sink->snapshot().size(), 1u);
}
