#ifndef HISTORY_H
#define HISTORY_H

#include "ScopeTree.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace profile {
  inline constexpr std::size_t maxCounters = 8;

  struct Sample {
    uint64_t sequence = 0;
    int64_t startNanos = 0;
    uint32_t wallMicros = 0;
    std::array<uint32_t, maxNodes> nodeMicros{};
    std::array<uint16_t, maxNodes> nodeCalls{};
    std::array<int64_t, maxCounters> counters{};
  };

  // Fixed ring, allocated up front.
  class History {
  public:
    explicit History(std::size_t capacity);

    void push(const Sample& sample);

    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::size_t capacity() const;
    [[nodiscard]] const Sample& at(std::size_t indexFromOldest) const;
    [[nodiscard]] const Sample& newest() const;
    [[nodiscard]] const Sample* findSequence(uint64_t sequence) const;

  private:
    std::vector<Sample> m_samples;
    std::size_t m_next = 0;
    std::size_t m_count = 0;
  };

  class DurationRing {
  public:
    explicit DurationRing(std::size_t capacity);

    void push(uint32_t micros);

    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] uint32_t at(std::size_t indexFromOldest) const;
    [[nodiscard]] std::vector<uint32_t> values() const;

  private:
    std::vector<uint32_t> m_values;
    std::size_t m_next = 0;
    std::size_t m_count = 0;
  };
}

#endif
