#include "TerrainParallel.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace terrain
{
  namespace
  {
    constexpr unsigned maxWorkers = 7;

    // Workers block on a condition variable between jobs, so an idle pool costs no CPU.
    class Pool
    {
    public:
      static Pool& instance()
      {
        static Pool pool;
        return pool;
      }

      void run(const int count, const std::function<void(int)>& body)
      {
        // A body that calls parallelFor again on the thread that holds the pool runs that call serially: try_lock on
        // a mutex the thread already owns is undefined.
        thread_local bool running = false;

        if (count < 2 || m_threads.empty() || running || !m_runMutex.try_lock())
        {
          for (int index = 0; index < count; ++index)
          {
            body(index);
          }

          return;
        }

        const std::lock_guard runLock(m_runMutex, std::adopt_lock);
        running = true;

        struct Reset
        {
          bool& flag;

          ~Reset()
          {
            flag = false;
          }
        } const reset{ running };

        {
          const std::lock_guard lock(m_mutex);
          m_body = &body;
          m_count = count;
          m_next.store(0, std::memory_order_relaxed);
          m_failure = nullptr;
          ++m_generation;
        }

        m_wake.notify_all();
        drain(body, count);

        std::unique_lock lock(m_mutex);
        m_body = nullptr;
        m_done.wait(lock, [this] { return m_active == 0; });

        if (m_failure)
        {
          std::rethrow_exception(std::exchange(m_failure, nullptr));
        }
      }

    private:
      Pool()
      {
        const unsigned cores = std::thread::hardware_concurrency();
        const unsigned workers = cores > 1 ? std::min(cores - 1, maxWorkers) : 0;

        for (unsigned index = 0; index < workers; ++index)
        {
          m_threads.emplace_back([this] { workerLoop(); });
        }
      }

      ~Pool()
      {
        {
          const std::lock_guard lock(m_mutex);
          m_stop = true;
        }

        m_wake.notify_all();

        for (auto& thread : m_threads)
        {
          thread.join();
        }
      }

      void workerLoop()
      {
        uint64_t seen = 0;
        std::unique_lock lock(m_mutex);

        while (true)
        {
          m_wake.wait(lock, [&] { return m_stop || m_generation != seen; });

          if (m_stop)
          {
            return;
          }

          seen = m_generation;

          if (!m_body)
          {
            continue;
          }

          const std::function<void(int)>& body = *m_body;
          const int count = m_count;

          ++m_active;
          lock.unlock();
          drain(body, count);
          lock.lock();

          if (--m_active == 0)
          {
            m_done.notify_all();
          }
        }
      }

      void drain(const std::function<void(int)>& body, const int count)
      {
        for (int index = m_next.fetch_add(1, std::memory_order_relaxed); index < count;
             index = m_next.fetch_add(1, std::memory_order_relaxed))
        {
          try
          {
            body(index);
          }
          catch (...)
          {
            const std::lock_guard lock(m_mutex);

            if (!m_failure)
            {
              m_failure = std::current_exception();
            }
          }
        }
      }

      std::mutex m_runMutex;
      std::mutex m_mutex;
      std::condition_variable m_wake;
      std::condition_variable m_done;
      std::vector<std::thread> m_threads;
      const std::function<void(int)>* m_body = nullptr;
      int m_count = 0;
      std::atomic<int> m_next{ 0 };
      int m_active = 0;
      uint64_t m_generation = 0;
      bool m_stop = false;
      std::exception_ptr m_failure;
    };
  }

  void parallelFor(const int count, const std::function<void(int)>& body)
  {
    Pool::instance().run(count, body);
  }
}
