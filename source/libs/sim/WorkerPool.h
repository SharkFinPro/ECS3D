#ifndef WORKERPOOL_H
#define WORKERPOOL_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// A fixed set of threads that sleep on a condition variable between jobs. OpenMP's workers spin between
// parallel regions, which at one region per tick keeps several cores at 100% for as long as a server runs.
class WorkerPool {
public:
  // The calling thread works too, so a pool of N threads starts N - 1 workers, on the first job that needs them.
  explicit WorkerPool(size_t threadCount);
  ~WorkerPool();

  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

  // Calls body(i) once for every i in [0, count), handing out chunkSize indices at a time, and returns when
  // all are done. The first exception a body throws is rethrown here once the other threads have stopped.
  void parallelFor(size_t count, size_t chunkSize, const std::function<void(size_t)>& body);

private:
  size_t m_threadCount;
  std::vector<std::thread> m_workers;

  std::mutex m_mutex;
  std::condition_variable m_wake;
  std::condition_variable m_done;
  bool m_stopping = false;
  size_t m_generation = 0;
  size_t m_active = 0;

  const std::function<void(size_t)>* m_body = nullptr;
  size_t m_count = 0;
  size_t m_chunkSize = 1;
  std::atomic<size_t> m_next = 0;

  std::mutex m_errorMutex;
  std::exception_ptr m_error;

  void startWorkers();
  void workerLoop();
  void runChunks();
};

#endif
