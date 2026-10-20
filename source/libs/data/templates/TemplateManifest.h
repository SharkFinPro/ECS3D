#ifndef TEMPLATEMANIFEST_H
#define TEMPLATEMANIFEST_H

#include <nlohmann/json_fwd.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

inline constexpr const char* templateManifestFileName = "template.json";

struct TemplatePlayers {
  int min = 1;
  int max = 1;
};

struct TemplateManifest {
  std::string id;
  std::string name;
  std::string description;
  // "template" or "sample".
  std::string category;
  std::vector<std::string> tags;
  std::string thumbnail;
  std::string entryScene;
  std::string minEngineVersion;
  // Absent means single-player.
  std::optional<TemplatePlayers> players;
};

// On failure returns nullopt and, when error is given, names the offending field. Unknown fields are
// ignored.
[[nodiscard]] std::optional<TemplateManifest> parseTemplateManifest(const nlohmann::json& data,
                                                                    std::string* error = nullptr);

// Reads folder / templateManifestFileName. Never throws: an unreadable or malformed file is reported
// through error.
[[nodiscard]] std::optional<TemplateManifest> loadTemplateManifest(const std::filesystem::path& folder,
                                                                   std::string* error = nullptr);

#endif //TEMPLATEMANIFEST_H
