#ifndef LANE_H
#define LANE_H

#include "History.h"
#include "ProfileScope.h"
#include "ScopeTree.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

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

    // Replaces any lane already bound to this thread. Unbind or destroy a lane on the thread it is bound to: destroying
    // it elsewhere leaves that thread's pointer dangling.
    void bindToThisThread();
    static void unbindThisThread();

    void beginSample();

    // close == false keeps the sample open; close == true finalizes and pushes it.
    void endSample(bool close);
    [[nodiscard]] bool sampleOpen() const;

    [[nodiscard]] const std::string& name() const;
    [[nodiscard]] const History& history() const;
    [[nodiscard]] const ScopeTree& tree() const;

    // Scopes folded into their parent by the node or depth cap.
    [[nodiscard]] uint64_t overflowCount() const;

    // Recorded scopes still open when their sample closed; folded ones are already in overflowCount().
    [[nodiscard]] uint64_t abandonedScopeCount() const;

    // Used by Scope. A folded scope gets a ticket whose depth field is zero, so its exit stays tied to its own
    // sample; a ticket of zero means no sample was open.
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

    std::array<Frame, maxDepth> m_stack{};
    std::size_t m_depth = 0;
    std::size_t m_foldedOpen = 0;

    uint64_t m_overflow = 0;
    uint64_t m_abandoned = 0;
  };
}

#endif
