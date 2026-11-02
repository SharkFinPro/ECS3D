#ifndef LANE_H
#define LANE_H

#include "History.h"
#include "ProfileScope.h"
#include "ScopeTree.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace profile {
  inline constexpr std::size_t maxDepth = 8;

  // One thread's timing record. Bind it to the thread that runs the scopes; read and write it only on that thread.
  class Lane {
  public:
    using NowFunction = std::function<Clock::time_point()>;

    Lane(std::string name, std::size_t historyCapacity, NowFunction now = &Clock::now);
    ~Lane();

    Lane(const Lane&) = delete;
    Lane& operator=(const Lane&) = delete;

    // Replaces any lane already bound to this thread.
    void bindToThisThread();
    static void unbindThisThread();

    // Opens a sample unless one is open.
    void beginSample();

    // close == false keeps the sample open; close == true finalizes and pushes it.
    void endSample(bool close);
    [[nodiscard]] bool sampleOpen() const;

    // Throws std::length_error past maxCounters.
    [[nodiscard]] uint8_t counterId(std::string_view name);

    // Ignored when no sample is open.
    void addCounter(uint8_t id, int64_t value);

    // Every completed scope with this name, at any depth, also pushes its duration into a ring of this capacity.
    void trackScope(NameId name, std::size_t capacity);
    [[nodiscard]] const DurationRing* tracked() const;

    [[nodiscard]] const std::string& name() const;
    [[nodiscard]] const History& history() const;
    [[nodiscard]] const ScopeTree& tree() const;

    // Scopes folded into their parent by the node or depth cap.
    [[nodiscard]] uint64_t overflowCount() const;

    // Scopes still open when their sample closed.
    [[nodiscard]] uint64_t abandonedScopeCount() const;

    // Used by Scope. A ticket is zero when nothing was opened (no sample); a folded scope gets a ticket whose depth
    // field is zero, so its exit stays tied to its own sample.
    [[nodiscard]] uint64_t enter(NameId name) noexcept;
    void exit(uint64_t ticket) noexcept;

  private:
    friend class Scope;

    struct Frame {
      NodeId node = 0;
      Clock::time_point start;
    };

    static thread_local Lane* s_bound;

    std::string m_name;
    NowFunction m_now;
    Clock::time_point m_created;
    History m_history;
    ScopeTree m_tree;

    bool m_open = false;
    uint64_t m_nextSequence = 1;
    uint64_t m_sequence = 0;
    Clock::time_point m_sampleStart;
    std::array<int64_t, maxNodes> m_nodeNanos{};
    std::array<uint32_t, maxNodes> m_nodeCalls{};
    std::array<int64_t, maxCounters> m_counterValues{};

    std::array<Frame, maxDepth> m_stack{};
    std::size_t m_depth = 0;
    std::size_t m_foldedOpen = 0;

    std::vector<std::string> m_counterNames;
    std::unique_ptr<DurationRing> m_tracked;
    NameId m_trackedName = 0;

    uint64_t m_overflow = 0;
    uint64_t m_abandoned = 0;
  };
}

#endif
