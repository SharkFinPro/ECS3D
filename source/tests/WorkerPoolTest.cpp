#include <gtest/gtest.h>

#include "WorkerPool.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

TEST(WorkerPool, RunsEveryIndexExactlyOnce)
{
  WorkerPool pool(4);
  std::vector<std::atomic<int>> hits(1000);

  pool.parallelFor(hits.size(), 8, [&](const size_t i) { ++hits[i]; });

  for (size_t i = 0; i < hits.size(); ++i)
  {
    EXPECT_EQ(hits[i], 1) << "index " << i;
  }
}

TEST(WorkerPool, SpreadsWorkAcrossThreads)
{
  WorkerPool pool(4);
  std::mutex mutex;
  std::set<std::thread::id> threads;

  // Each index holds its thread long enough that the others get a turn, so more than one thread runs.
  pool.parallelFor(64, 1, [&](size_t)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    std::lock_guard lock(mutex);
    threads.insert(std::this_thread::get_id());
  });

  EXPECT_GT(threads.size(), 1u);
}

TEST(WorkerPool, RunsOnlyOnTheCallingThreadWithASingleThread)
{
  WorkerPool pool(1);
  std::set<std::thread::id> threads;

  pool.parallelFor(100, 4, [&](size_t) { threads.insert(std::this_thread::get_id()); });

  EXPECT_EQ(threads, std::set{ std::this_thread::get_id() });
}

TEST(WorkerPool, TreatsAZeroThreadCountAsOne)
{
  WorkerPool pool(0);
  int total = 0;

  pool.parallelFor(10, 1, [&](const size_t i) { total += static_cast<int>(i); });

  EXPECT_EQ(total, 45);
}

TEST(WorkerPool, DoesNothingForAnEmptyRange)
{
  WorkerPool pool(4);
  std::atomic<int> calls = 0;

  pool.parallelFor(0, 8, [&](size_t) { ++calls; });

  EXPECT_EQ(calls, 0);
}

TEST(WorkerPool, TreatsAZeroChunkSizeAsOne)
{
  WorkerPool pool(4);
  std::vector<std::atomic<int>> hits(50);

  pool.parallelFor(hits.size(), 0, [&](const size_t i) { ++hits[i]; });

  for (const auto& hit : hits)
  {
    EXPECT_EQ(hit, 1);
  }
}

TEST(WorkerPool, CanRunManyJobsInARow)
{
  WorkerPool pool(4);
  std::atomic<size_t> total = 0;

  for (int job = 0; job < 200; ++job)
  {
    pool.parallelFor(100, 8, [&](size_t) { ++total; });
  }

  EXPECT_EQ(total, 200u * 100u);
}

TEST(WorkerPool, RethrowsABodyException)
{
  WorkerPool pool(4);

  EXPECT_THROW(pool.parallelFor(500, 4, [](const size_t i)
  {
    if (i == 137)
    {
      throw std::runtime_error("boom");
    }
  }), std::runtime_error);

  // Positive control: the pool is still usable after a job failed.
  std::atomic<int> calls = 0;
  pool.parallelFor(100, 4, [&](size_t) { ++calls; });
  EXPECT_EQ(calls, 100);
}

TEST(WorkerPool, RethrowsAnExceptionFromTheSerialPath)
{
  WorkerPool pool(1);

  EXPECT_THROW(pool.parallelFor(10, 1, [](size_t) { throw std::runtime_error("boom"); }), std::runtime_error);
}
