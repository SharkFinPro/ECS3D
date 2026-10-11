#ifndef SCENESCRIPTSPANELLOGIC_H
#define SCENESCRIPTSPANELLOGIC_H

#include <assets/AssetRegistry.h>
#include <objects/components/Script.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
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
}

#endif //SCENESCRIPTSPANELLOGIC_H
