#include "Lane.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace profile {
  namespace {
    // Low byte of a ticket is the stack depth; the rest is the sample sequence.
    constexpr uint64_t depthMask = 0xFF;

    template <typename T>
    T saturateTo(const int64_t value)
    {
      return static_cast<T>(std::clamp<int64_t>(value, 0, std::numeric_limits<T>::max()));
    }

    int64_t toNanos(const Clock::duration duration)
    {
      return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    }
  }

  thread_local Lane* Lane::s_bound = nullptr;

  Lane::Lane(std::string name, const std::size_t historyCapacity, NowFunction now)
    : m_name(std::move(name)),
      m_now(std::move(now)),
      m_created(m_now()),
      m_history(historyCapacity)
  {
  }

  Lane::~Lane()
  {
    if (s_bound == this)
    {
      s_bound = nullptr;
    }
  }

  void Lane::bindToThisThread()
  {
    s_bound = this;
  }

  void Lane::unbindThisThread()
  {
    s_bound = nullptr;
  }

  void Lane::beginSample()
  {
    if (m_open)
    {
      return;
    }

    m_open = true;
    m_sequence = m_nextSequence++;
    m_sampleStart = m_now();
    m_nodeNanos.fill(0);
    m_nodeCalls.fill(0);
    m_depth = 0;
    m_foldedOpen = 0;
  }

  void Lane::endSample(const bool close)
  {
    if (!m_open || !close)
    {
      return;
    }

    Sample sample;
    sample.sequence = m_sequence;
    sample.startNanos = toNanos(m_sampleStart - m_created);
    sample.wallMicros = saturateTo<uint32_t>(toNanos(m_now() - m_sampleStart) / 1000);

    for (std::size_t i = 0; i < maxNodes; ++i)
    {
      sample.nodeMicros[i] = saturateTo<uint32_t>(m_nodeNanos[i] / 1000);
      sample.nodeCalls[i] = saturateTo<uint16_t>(m_nodeCalls[i]);
    }

    m_history.push(sample);

    m_abandoned += m_depth;
    m_depth = 0;
    m_foldedOpen = 0;
    m_open = false;
  }

  bool Lane::sampleOpen() const
  {
    return m_open;
  }

  const std::string& Lane::name() const
  {
    return m_name;
  }

  const History& Lane::history() const
  {
    return m_history;
  }

  const ScopeTree& Lane::tree() const
  {
    return m_tree;
  }

  uint64_t Lane::overflowCount() const
  {
    return m_overflow;
  }

  uint64_t Lane::abandonedScopeCount() const
  {
    return m_abandoned;
  }

  uint64_t Lane::enter(const NameId name) noexcept
  {
    if (!m_open)
    {
      return 0;
    }

    const uint64_t foldedTicket = m_sequence << 8;

    if (m_foldedOpen > 0 || m_depth >= maxDepth)
    {
      ++m_overflow;
      ++m_foldedOpen;
      return foldedTicket;
    }

    const NodeId parent = m_depth > 0 ? m_stack[m_depth - 1].node : NodeId{0};
    std::optional<NodeId> node;

    try
    {
      node = m_tree.child(parent, name);
    }
    catch (...)
    {
      node = std::nullopt;
    }

    if (!node)
    {
      ++m_overflow;
      ++m_foldedOpen;
      return foldedTicket;
    }

    m_stack[m_depth] = {*node, m_now()};
    ++m_depth;
    return foldedTicket | m_depth;
  }

  void Lane::exit(const uint64_t ticket) noexcept
  {
    if (ticket == 0 || !m_open || (ticket >> 8) != m_sequence)
    {
      return;
    }

    const auto depth = static_cast<std::size_t>(ticket & depthMask);

    if (depth == 0)
    {
      if (m_foldedOpen > 0)
      {
        --m_foldedOpen;
      }

      return;
    }

    if (depth != m_depth || m_foldedOpen > 0)
    {
      return;
    }

    const auto frame = m_stack[m_depth - 1];
    --m_depth;

    m_nodeNanos[frame.node] += toNanos(m_now() - frame.start);
    ++m_nodeCalls[frame.node];
  }
}
