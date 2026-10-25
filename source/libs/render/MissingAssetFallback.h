#ifndef MISSINGASSETFALLBACK_H
#define MISSINGASSETFALLBACK_H

#include <Log.h>
#include <memory>
#include <string>
#include <unordered_set>

template <typename Model, typename Texture>
struct RenderAssets {
  std::shared_ptr<Model> model;
  std::shared_ptr<Texture> texture;
  std::shared_ptr<Texture> specularMap;

  [[nodiscard]] bool operator==(const RenderAssets&) const = default;
};

// Fills each missing slot with a placeholder. A missing model draws the placeholder model wearing the placeholder
// texture, so it reads as missing whatever its own texture is. A placeholder that is itself null leaves its slot null.
template <typename Model, typename Texture>
[[nodiscard]] RenderAssets<Model, Texture> withPlaceholders(RenderAssets<Model, Texture> assets,
                                                            const std::shared_ptr<Model>& placeholderModel,
                                                            const std::shared_ptr<Texture>& placeholderTexture)
{
  if (!assets.model)
  {
    assets.model = placeholderModel;
    assets.texture = placeholderTexture;
  }

  if (!assets.texture)
  {
    assets.texture = placeholderTexture;
  }

  if (!assets.specularMap)
  {
    assets.specularMap = placeholderTexture;
  }

  return assets;
}

// Logs each missing asset once per key, so a placeholder drawn every frame does not flood the log.
class MissingAssetLog {
public:
  void report(const std::string& key, const std::string& label)
  {
    if (m_reported.insert(key).second)
    {
      Log::warn(LogCategory::assets, "Missing asset " + label + "; drawing the placeholder instead");
    }
  }

  void clear()
  {
    m_reported.clear();
  }

private:
  std::unordered_set<std::string> m_reported;
};

#endif //MISSINGASSETFALLBACK_H
