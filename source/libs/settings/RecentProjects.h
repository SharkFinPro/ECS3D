#ifndef RECENTPROJECTS_H
#define RECENTPROJECTS_H

#include <nlohmann/json.hpp>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

// The recent-projects list the editor and the launcher both read and write. The file schema and the
// writer rules are documented in RecentProjects.md.
//
// Not thread safe, and not synchronized with another process: two writers race and the last write wins.
struct RecentProjectEntry {
  std::string path;
  std::string name;
  std::string lastOpenedUtc;
  bool pinned = false;
  std::string templateId;
};

class RecentProjects {
public:
  static constexpr std::size_t maxEntries = 50;

  // recent-projects.json in the directory userDataDirectory() resolves.
  [[nodiscard]] static std::filesystem::path defaultFile();

  explicit RecentProjects(std::filesystem::path file);

  // Replaces the in-memory list with the file's. A missing file is empty; an unreadable one is empty too
  // and is logged once, then replaced by the next save.
  void load();

  // Writes atomically. Returns false, after logging, when the file could not be written.
  [[nodiscard]] bool save() const;

  // Moves the entry for this path to the front (or inserts one), stamps the time, recomputes the name and
  // applies the cap. The entry's pinned, template and unknown fields are kept; if the list holds several
  // entries for the path, the first is kept and the others are removed. A project file such as
  // project.ecs3d is recorded as its folder. The entry just recorded is never evicted by the cap, so a
  // list of pinned entries grows by one.
  void recordOpened(const std::filesystem::path& projectPath, std::chrono::system_clock::time_point now);

  [[nodiscard]] std::vector<RecentProjectEntry> entries() const;

  [[nodiscard]] const std::filesystem::path& file() const;

  // "2026-10-18T19:02:11Z", independent of the local time zone.
  [[nodiscard]] static std::string formatUtc(std::chrono::system_clock::time_point time);

private:
  std::filesystem::path m_file;

  nlohmann::json m_extra = nlohmann::json::object();
  std::vector<nlohmann::json> m_projects;
};

// load -> recordOpened(now) -> save on the default file. Returns whether the save succeeded.
bool recordProjectOpened(const std::filesystem::path& projectPath);

#endif //RECENTPROJECTS_H
