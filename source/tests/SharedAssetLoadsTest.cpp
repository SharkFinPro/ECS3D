#include <gtest/gtest.h>

#include <Log.h>
#include <RingBufferSink.h>
#include <SharedAssetLoads.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

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

TEST_F(SharedAssetLoadsTest, KeysWithTheSameLabelLoadIndependently)
{
  int calls = 0;
  const auto load = [&calls] {
    return std::make_shared<int>(++calls);
  };

  const auto first = m_loads.get("uuid-a", "assets/models/m.glb", load);
  const auto second = m_loads.get("uuid-b", "assets/models/m.glb", load);

  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_NE(first, second);
  EXPECT_EQ(calls, 2);
}

TEST_F(SharedAssetLoadsTest, KeyedFailureLogsOnceNamingTheLabelNotTheKey)
{
  int calls = 0;
  const auto load = [&calls]() -> std::shared_ptr<int> {
    ++calls;
    throw std::runtime_error("corrupt");
  };

  EXPECT_EQ(m_loads.get("key-1234", "assets/models/m.glb", load), nullptr);
  EXPECT_EQ(m_loads.get("key-1234", "assets/models/m.glb", load), nullptr);

  EXPECT_EQ(calls, 1);
  const auto entries = m_sink->snapshot();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_NE(entries[0].message.find("assets/models/m.glb"), std::string::npos);
  EXPECT_EQ(entries[0].message.find("key-1234"), std::string::npos);
}

TEST_F(SharedAssetLoadsTest, DrainReleasesEverySuccessAndForgetsFailuresToo)
{
  int goodCalls = 0;
  int badCalls = 0;
  const auto good = [&goodCalls] {
    return std::make_shared<int>(++goodCalls);
  };
  const auto bad = [&badCalls]() -> std::shared_ptr<int> {
    ++badCalls;
    throw std::runtime_error("nope");
  };

  const auto first = m_loads.get("good.glb", good);
  EXPECT_EQ(m_loads.get("bad.glb", bad), nullptr);
  ASSERT_NE(first, nullptr);

  std::vector<std::shared_ptr<int>> released;
  m_loads.drain([&released](std::shared_ptr<int> asset) { released.push_back(std::move(asset)); });

  ASSERT_EQ(released.size(), 1u);
  EXPECT_EQ(released[0], first);

  // Positive control: the load map is empty again, so both paths load afresh.
  const auto second = m_loads.get("good.glb", good);
  EXPECT_NE(second, first);
  EXPECT_EQ(goodCalls, 2);
  EXPECT_EQ(m_loads.get("bad.glb", bad), nullptr);
  EXPECT_EQ(badCalls, 2);

  std::vector<std::shared_ptr<int>> releasedAgain;
  m_loads.drain([&releasedAgain](std::shared_ptr<int> asset) { releasedAgain.push_back(std::move(asset)); });
  ASSERT_EQ(releasedAgain.size(), 1u);
  EXPECT_EQ(releasedAgain[0], second);
}

TEST_F(SharedAssetLoadsTest, KeyedSuccessIsCachedPerKey)
{
  int calls = 0;
  const auto load = [&calls] {
    return std::make_shared<int>(++calls);
  };

  const auto a = m_loads.get("k1", "label", load);
  EXPECT_EQ(m_loads.get("k1", "label", load), a);
  EXPECT_EQ(calls, 1);

  const auto b = m_loads.get("k2", "label", load);
  EXPECT_NE(a, b);
  EXPECT_EQ(calls, 2);
}
