#ifndef ASSETDISPLAY_H
#define ASSETDISPLAY_H

#include "EditorTheme.h"
#include "GuiComponents.h"
#include <assets/AssetRegistry.h>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>

// Shared per-type presentation for asset records, so the asset browser tiles and the Inspector's asset
// views describe an asset the same way (type label, display name, icon, accent color). Header-only so
// both the browser and each asset inspector pull from one source and can't drift.
namespace assetDisplay {
  inline const char* typeLabel(const AssetType type)
  {
    switch (type)
    {
      case AssetType::Model:   return "Model";
      case AssetType::Texture: return "Texture";
      case AssetType::Scene:   return "Scene";
      case AssetType::Script:  return "Script";
      case AssetType::Prefab:  return "Prefab";
      default:                 return "Unknown";
    }
  }

  // The display name shown in the browser tile + inspector header. A rename sets an override that wins
  // over the type-derived name; the file on disk and `path` (the registry key) are untouched.
  inline std::string name(const AssetRecord& record)
  {
    if (!record.displayName.empty())
    {
      return record.displayName;
    }

    switch (record.type)
    {
      // Scenes and prefabs store their display name in `path` (neither is a file on disk).
      case AssetType::Scene:
      case AssetType::Prefab: return record.path;
      case AssetType::Script: return record.className;
      default:                return std::filesystem::path(record.path).filename().string();
    }
  }

  inline gc::SecIcon icon(const AssetType type)
  {
    switch (type)
    {
      case AssetType::Texture: return gc::SecIcon::image;
      case AssetType::Model:   return gc::SecIcon::model;
      case AssetType::Script:  return gc::SecIcon::script;
      case AssetType::Scene:   return gc::SecIcon::scene;
      case AssetType::Prefab:  return gc::SecIcon::block;
      default:                 return gc::SecIcon::none;
    }
  }

  inline ImVec4 color(const AssetType type)
  {
    switch (type)
    {
      case AssetType::Model:  return theme::modelPurple;
      case AssetType::Script: return theme::scriptAmber;
      case AssetType::Scene:  return theme::sceneGreen;
      case AssetType::Prefab: return theme::prefabBlue;
      case AssetType::Texture:
      default:                return theme::accent;
    }
  }

  // A compact human-readable byte count (B / KB / MB) for a file-size row.
  inline std::string fileSizeString(const std::uintmax_t bytes)
  {
    char buf[32];
    if (bytes < 1024)
    {
      std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    }
    else if (bytes < 1024 * 1024)
    {
      std::snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    }
    else
    {
      std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return buf;
  }

  // The size of a file-backed asset on disk, or nullopt when the file isn't present editor-side (the
  // editor and server may share no filesystem).
  inline std::optional<std::string> fileSize(const std::string& path)
  {
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(path, ec);
    if (ec)
    {
      return std::nullopt;
    }
    return fileSizeString(bytes);
  }
}

#endif //ASSETDISPLAY_H
