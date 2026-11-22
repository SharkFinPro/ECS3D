#ifndef PROCESSCPUCLOCK_H
#define PROCESSCPUCLOCK_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <ctime>
#endif

namespace testclock {
  // CPU seconds this process has used. Unlike a wall clock it does not grow when ctest runs many test processes at
  // once on a busy machine, so a speed bound on single-threaded work measures the work and not the contention.
  inline double processCpuSeconds()
  {
#ifdef _WIN32
    FILETIME creation{}, exit{}, kernel{}, user{};
    GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user);
    const auto ticks = [](const FILETIME& time) {
      return static_cast<double>((static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime);
    };
    return (ticks(kernel) + ticks(user)) * 1e-7;
#else
    timespec spec{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &spec);
    return static_cast<double>(spec.tv_sec) + static_cast<double>(spec.tv_nsec) * 1e-9;
#endif
  }
}

#endif
