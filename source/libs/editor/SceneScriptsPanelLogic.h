#ifndef SCENESCRIPTSPANELLOGIC_H
#define SCENESCRIPTSPANELLOGIC_H

#include <assets/AssetRegistry.h>
#include <objects/components/Script.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// The decisions behind the "Scene Scripts" window, free of ImGui so the suite can test them.
namespace sceneScriptPanel {

  // Script asset classes the scene does not carry yet, sorted and without repeats. A scene holds at most
  // one script per class, and two assets may name the same class.
  [[nodiscard]] inline std::vector<std::string> availableClasses(
    const AssetRegistry& registry, const std::vector<std::shared_ptr<Script>>& attached)
  {
    std::vector<std::string> classes;

    for (const auto& [uuid, record] : registry.getAssets())
    {
      if (record.type != AssetType::Script || record.className.empty())
      {
        continue;
      }

      const bool alreadyAttached = std::ranges::any_of(attached, [&record](const std::shared_ptr<Script>& script) {
        return script->getClassName() == record.className;
      });

      if (!alreadyAttached)
      {
        classes.push_back(record.className);
      }
    }

    std::ranges::sort(classes);
    classes.erase(std::ranges::unique(classes).begin(), classes.end());

    return classes;
  }

  // The index to send for moving the entry at `index` one place, or nothing at the edge of the list. The
  // index is read against the list with the entry removed (SceneAsset::moveScript), so the neighbor's
  // index is the right target in both directions.
  [[nodiscard]] inline std::optional<std::size_t> reorderTarget(const std::size_t index, const std::size_t count,
                                                                const bool up)
  {
    if (index >= count)
    {
      return std::nullopt;
    }

    if (up)
    {
      return index > 0 ? std::optional(index - 1) : std::nullopt;
    }

    return index + 1 < count ? std::optional(index + 1) : std::nullopt;
  }

  // A coalesced field edit goes out once the gesture is over (no widget held) and the panel's copy
  // actually differs from what the server last replicated.
  [[nodiscard]] inline bool shouldSendFieldEdit(const nlohmann::json* editCopy, const nlohmann::json& replicated,
                                                const bool anyWidgetActive)
  {
    return editCopy != nullptr && !anyWidgetActive && *editCopy != replicated;
  }

  using Clock = std::chrono::steady_clock;
  using NowFunction = std::function<Clock::time_point()>;

  // How long a sent edit is waited on before the panel stops expecting its echo.
  inline constexpr std::chrono::milliseconds defaultPendingTimeout{500};

  // The panel-owned field blobs being edited, by class name. A copy is shown instead of the replicated
  // blob, because setSceneScriptFields carries the whole blob: a second edit built from the stale
  // replicated value before the first one's echo would revert the first. A sent copy is kept (and not
  // resent) until the replicated blob equals it, the class goes away, or the timeout passes.
  class FieldEditTracker {
  public:
    using ReplicatedLookup = std::function<const nlohmann::json*(const std::string& className)>;
    using Send = std::pair<std::string, nlohmann::json>;

    explicit FieldEditTracker(const std::chrono::milliseconds timeout = defaultPendingTimeout,
                              NowFunction now = &Clock::now)
      : m_timeout(timeout),
        m_now(std::move(now))
    {}

    // The blob to show for the class, nullptr when the replicated one should be shown.
    [[nodiscard]] const nlohmann::json* shown(const std::string& className) const
    {
      const auto it = m_entries.find(className);
      return it == m_entries.end() ? nullptr : &it->second.copy;
    }

    // Records an edit made through a widget; a sent copy becomes unsent so the change goes out on release.
    void edit(const std::string& className, nlohmann::json fields)
    {
      m_entries[className] = Entry{ std::move(fields), false, {} };
    }

    // Drops entries that are echoed, orphaned or timed out, and returns the copies to send now: unsent
    // ones that differ from the replicated blob once no widget is active. Those are flagged sent.
    [[nodiscard]] std::vector<Send> poll(const ReplicatedLookup& replicatedFor, const bool anyWidgetActive)
    {
      std::vector<Send> toSend;

      for (auto it = m_entries.begin(); it != m_entries.end();)
      {
        auto& [className, entry] = *it;
        const nlohmann::json* replicated = replicatedFor(className);

        const bool gone = replicated == nullptr;
        const bool echoed = !gone && entry.copy == *replicated && (entry.sent || !anyWidgetActive);
        const bool expired = entry.sent && m_now() - entry.sentAt >= m_timeout;

        if (gone || echoed || expired)
        {
          it = m_entries.erase(it);
          continue;
        }

        if (!entry.sent && shouldSendFieldEdit(&entry.copy, *replicated, anyWidgetActive))
        {
          entry.sent = true;
          entry.sentAt = m_now();
          toSend.emplace_back(className, entry.copy);
        }

        ++it;
      }

      return toSend;
    }

    void erase(const std::string& className)
    {
      m_entries.erase(className);
    }

    void clear()
    {
      m_entries.clear();
    }

    [[nodiscard]] bool empty() const
    {
      return m_entries.empty();
    }

  private:
    struct Entry {
      nlohmann::json copy;
      bool sent = false;
      Clock::time_point sentAt;
    };

    std::chrono::milliseconds m_timeout;
    NowFunction m_now;
    std::unordered_map<std::string, Entry> m_entries;
  };

  // After a structural send (add, remove, reorder) further structural clicks are ignored until the
  // replicated list order changes or the timeout passes: the editor validates and records undo against its
  // replicated view, so a second click inside one round trip would send and record a duplicate.
  class StructuralEditGate {
  public:
    explicit StructuralEditGate(const std::chrono::milliseconds timeout = defaultPendingTimeout,
                                NowFunction now = &Clock::now)
      : m_timeout(timeout),
        m_now(std::move(now))
    {}

    [[nodiscard]] bool blocked(const std::vector<std::string>& currentOrder)
    {
      if (m_pending && (currentOrder != m_orderAtSend || m_now() - m_pendingSince >= m_timeout))
      {
        m_pending = false;
      }

      return m_pending;
    }

    void begin(const std::vector<std::string>& currentOrder)
    {
      m_pending = true;
      m_orderAtSend = currentOrder;
      m_pendingSince = m_now();
    }

    void clear()
    {
      m_pending = false;
    }

  private:
    std::chrono::milliseconds m_timeout;
    NowFunction m_now;
    bool m_pending = false;
    std::vector<std::string> m_orderAtSend;
    Clock::time_point m_pendingSince;
  };
}

#endif //SCENESCRIPTSPANELLOGIC_H
