#ifndef PROFILESCOPE_H
#define PROFILESCOPE_H

#include <chrono>
#include <cstdint>
#include <string_view>

#ifndef ECS3D_PROFILER_SCOPES
#define ECS3D_PROFILER_SCOPES 1
#endif

namespace profile {
  using Clock = std::chrono::steady_clock;
  using NameId = uint16_t;

  // Thread safe. Called once per call site through a function-local static; the same text gives the same id.
  [[nodiscard]] NameId internName(std::string_view name);

  // Valid for the program's lifetime.
  [[nodiscard]] std::string_view nameOf(NameId id);

  void setEnabled(bool enabled);
  [[nodiscard]] bool isEnabled();

  class Lane;

  // Times construction to destruction into the lane bound to the calling thread. Does nothing when profiling is
  // disabled, when no lane is bound to this thread, or when that lane has no open sample. The lane must outlive it.
  class Scope {
  public:
    explicit Scope(NameId name) noexcept;
    ~Scope();

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

  private:
    Lane* m_lane = nullptr;
    uint64_t m_ticket = 0;
  };
}

#define ECS3D_PROFILE_CONCAT_INNER(a, b) a##b
#define ECS3D_PROFILE_CONCAT(a, b) ECS3D_PROFILE_CONCAT_INNER(a, b)

#if ECS3D_PROFILER_SCOPES
#define ECS3D_PROFILE_SCOPE(name) \
  static const ::profile::NameId ECS3D_PROFILE_CONCAT(ecs3dProfileName_, __LINE__) = ::profile::internName(name); \
  const ::profile::Scope ECS3D_PROFILE_CONCAT(ecs3dProfileScope_, __LINE__)( \
    ECS3D_PROFILE_CONCAT(ecs3dProfileName_, __LINE__))
#else
#define ECS3D_PROFILE_SCOPE(name) static_cast<void>(0)
#endif

#endif
