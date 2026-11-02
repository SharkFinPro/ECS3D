#include "ProfileScope.h"
#include "Lane.h"

#include <atomic>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace profile {
  namespace {
    struct NameRegistry {
      std::mutex mutex;
      std::deque<std::string> names;
      std::unordered_map<std::string_view, NameId> ids;
    };

    NameRegistry& registry()
    {
      static NameRegistry instance;
      return instance;
    }

    std::atomic<bool> g_enabled{true};
  }

  NameId internName(const std::string_view name)
  {
    auto& reg = registry();
    const std::lock_guard lock(reg.mutex);

    if (const auto it = reg.ids.find(name); it != reg.ids.end())
    {
      return it->second;
    }

    if (reg.names.size() > std::numeric_limits<NameId>::max())
    {
      throw std::length_error("too many profile scope names");
    }

    const auto id = static_cast<NameId>(reg.names.size());
    reg.names.emplace_back(name);
    reg.ids.emplace(reg.names.back(), id);
    return id;
  }

  std::string_view nameOf(const NameId id)
  {
    auto& reg = registry();
    const std::lock_guard lock(reg.mutex);

    if (id >= reg.names.size())
    {
      return {};
    }

    return reg.names[id];
  }

  void setEnabled(const bool enabled)
  {
    g_enabled.store(enabled, std::memory_order_relaxed);
  }

  bool isEnabled()
  {
    return g_enabled.load(std::memory_order_relaxed);
  }

  Scope::Scope(const NameId name) noexcept
  {
    Lane* const lane = Lane::s_bound;

    if (lane == nullptr || !isEnabled())
    {
      return;
    }

    const auto ticket = lane->enter(name);

    if (ticket != 0)
    {
      m_lane = lane;
      m_ticket = ticket;
    }
  }

  Scope::~Scope()
  {
    if (m_lane != nullptr)
    {
      m_lane->exit(m_ticket);
    }
  }
}
