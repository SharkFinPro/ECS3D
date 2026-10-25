#include "WorkerPool.h"
#include <algorithm>

WorkerPool::WorkerPool(const size_t threadCount)
  : m_threadCount(std::max<size_t>(threadCount, 1))
{}

WorkerPool::~WorkerPool()
{
  {
    std::lock_guard lock(m_mutex);
    m_stopping = true;
  }

  m_wake.notify_all();

  for (auto& worker : m_workers)
  {
    worker.join();
  }
}

void WorkerPool::parallelFor(const size_t count, const size_t chunkSize, const std::function<void(size_t)>& body)
{
  const auto chunk = std::max<size_t>(chunkSize, 1);

  if (m_threadCount == 1 || count <= chunk)
  {
    for (size_t i = 0; i < count; ++i)
    {
      body(i);
    }

    return;
  }

  startWorkers();

  {
    std::lock_guard lock(m_mutex);
    m_body = &body;
    m_count = count;
    m_chunkSize = chunk;
    m_next = 0;
    m_error = nullptr;
    m_active = m_workers.size();
    ++m_generation;
  }

  m_wake.notify_all();

  runChunks();

  std::unique_lock lock(m_mutex);
  m_done.wait(lock, [this] { return m_active == 0; });
  m_body = nullptr;

  if (m_error)
  {
    std::rethrow_exception(m_error);
  }
}

void WorkerPool::startWorkers()
{
  if (!m_workers.empty())
  {
    return;
  }

  m_workers.reserve(m_threadCount - 1);
  for (size_t i = 1; i < m_threadCount; ++i)
  {
    m_workers.emplace_back([this] { workerLoop(); });
  }
}

void WorkerPool::workerLoop()
{
  size_t seen = 0;
  std::unique_lock lock(m_mutex);

  while (true)
  {
    m_wake.wait(lock, [&] { return m_stopping || m_generation != seen; });

    if (m_stopping)
    {
      return;
    }

    seen = m_generation;

    lock.unlock();
    runChunks();
    lock.lock();

    if (--m_active == 0)
    {
      m_done.notify_one();
    }
  }
}

void WorkerPool::runChunks()
{
  while (true)
  {
    const auto begin = m_next.fetch_add(m_chunkSize);
    if (begin >= m_count)
    {
      return;
    }

    const auto end = std::min(begin + m_chunkSize, m_count);

    try
    {
      for (auto i = begin; i < end; ++i)
      {
        (*m_body)(i);
      }
    }
    catch (...)
    {
      {
        std::lock_guard lock(m_errorMutex);
        if (!m_error)
        {
          m_error = std::current_exception();
        }
      }

      m_next = m_count;
    }
  }
}
