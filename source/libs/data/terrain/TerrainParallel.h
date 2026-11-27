#ifndef TERRAINPARALLEL_H
#define TERRAINPARALLEL_H

#include <functional>

namespace terrain
{
  // Runs body(0) .. body(count - 1) on the calling thread and a small pool of workers that sleep between calls, and
  // returns once every call has finished. Calls may run in any order and concurrently, so each must touch only its own
  // output. Runs serially when count is below 2, the machine has one core, another caller holds the pool, or it is
  // called from inside a body. Rethrows
  // the first exception a call threw, after the rest have finished.
  void parallelFor(int count, const std::function<void(int)>& body);
}

#endif //TERRAINPARALLEL_H
