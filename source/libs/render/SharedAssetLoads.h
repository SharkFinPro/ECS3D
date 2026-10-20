#ifndef SHAREDASSETLOADS_H
#define SHAREDASSETLOADS_H

#include <Log.h>
#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

// Loads each path once and shares the result. A load that throws (the engine's loaders throw on a
// missing or invalid file rather than returning null) or returns null is logged once and remembered,
// so the disk is not retried every frame.
template <typename T>
class SharedAssetLoads {
public:
  template <typename Loader>
  [[nodiscard]] std::shared_ptr<T> get(const std::string& path, Loader&& loader)
  {
    return get(path, path, std::forward<Loader>(loader));
  }

  // Caches under key but names label in the log, for assets identified by something other than their path.
  template <typename Loader>
  [[nodiscard]] std::shared_ptr<T> get(const std::string& key, const std::string& label, Loader&& loader)
  {
    if (const auto it = m_loads.find(key); it != m_loads.end())
    {
      return it->second;
    }

    std::shared_ptr<T> asset;

    try
    {
      asset = std::forward<Loader>(loader)();
      if (!asset)
      {
        Log::error(LogCategory::assets, "Failed to load '" + label + "'");
      }
    }
    catch (const std::exception& e)
    {
      Log::error(LogCategory::assets, "Failed to load '" + label + "': " + e.what());
    }

    m_loads.emplace(key, asset);

    return asset;
  }

  // Hands every loaded asset to release, then forgets all loads (failed ones included, so they are retried).
  template <typename Release>
  void drain(Release&& release)
  {
    for (auto& entry : m_loads)
    {
      if (entry.second)
      {
        release(std::move(entry.second));
      }
    }

    m_loads.clear();
  }

private:
  std::unordered_map<std::string, std::shared_ptr<T>> m_loads;
};

#endif //SHAREDASSETLOADS_H
