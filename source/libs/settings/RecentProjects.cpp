#include "RecentProjects.h"
#include <Log.h>
#include <UserDataDirectory.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <utility>

namespace {
  constexpr int currentVersion = 1;

  // Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's days_from_civil).
  std::int64_t daysFromCivil(std::int64_t year, const unsigned month, const unsigned day)
  {
    year -= month <= 2 ? 1 : 0;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const auto yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned dayOfYear = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;

    return era * 146097 + static_cast<std::int64_t>(dayOfEra) - 719468;
  }

  struct CivilDate {
    std::int64_t year;
    unsigned month;
    unsigned day;
  };

  CivilDate civilFromDays(std::int64_t days)
  {
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const auto dayOfEra = static_cast<unsigned>(days - era * 146097);
    const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const unsigned monthIndex = (5 * dayOfYear + 2) / 153;
    const unsigned day = dayOfYear - (153 * monthIndex + 2) / 5 + 1;
    const unsigned month = monthIndex < 10 ? monthIndex + 3 : monthIndex - 9;
    const std::int64_t year = static_cast<std::int64_t>(yearOfEra) + era * 400 + (month <= 2 ? 1 : 0);

    return { year, month, day };
  }

  // Seconds since the epoch, or the minimum value for anything that is not the exact written format,
  // so a missing or garbled time sorts as the oldest.
  std::int64_t parseUtc(const nlohmann::json& entry)
  {
    constexpr auto oldest = std::numeric_limits<std::int64_t>::min();

    const auto it = entry.find("lastOpenedUtc");
    if (it == entry.end() || !it->is_string())
    {
      return oldest;
    }

    const auto text = it->get<std::string>();
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int consumed = 0;

    if (text.size() != 20
        || std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2dZ%n", &year, &month, &day, &hour, &minute,
                       &second, &consumed) != 6
        || consumed != 20)
    {
      return oldest;
    }

    if (year < 0 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0
        || minute > 59 || second < 0 || second > 60)
    {
      return oldest;
    }

    return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400
           + hour * 3600 + minute * 60 + second;
  }

  bool isPinned(const nlohmann::json& entry)
  {
    const auto it = entry.find("pinned");
    return it != entry.end() && it->is_boolean() && it->get<bool>();
  }

  std::string comparisonKey(std::string path)
  {
#if defined(_WIN32) || defined(__APPLE__)
    for (auto& c : path)
    {
      if (c >= 'A' && c <= 'Z')
      {
        c = static_cast<char>(c - 'A' + 'a');
      }
    }
#endif
    return path;
  }

  std::string normalizedPath(const std::filesystem::path& projectPath)
  {
    std::error_code error;
    auto absolute = std::filesystem::absolute(projectPath, error);
    if (error)
    {
      absolute = projectPath;
    }

    auto text = absolute.lexically_normal().generic_string();
    while (text.size() > 1 && text.back() == '/')
    {
      text.pop_back();
    }

    return text;
  }

  bool hasProjectFileExtension(std::string extension)
  {
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return extension == ".json" || extension == ".ecs3d";
  }

  std::string displayName(const std::string& normalized)
  {
    const std::filesystem::path path(normalized);
    if (hasProjectFileExtension(path.extension().generic_string()))
    {
      return path.stem().generic_string();
    }

    return path.filename().generic_string();
  }

  void removeQuietly(const std::filesystem::path& path)
  {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }
}

std::filesystem::path RecentProjects::defaultFile()
{
  return userDataDirectory() / "recent-projects.json";
}

RecentProjects::RecentProjects(std::filesystem::path file)
  : m_file(std::move(file))
{
}

void RecentProjects::load()
{
  m_extra = nlohmann::json::object();
  m_projects.clear();

  std::error_code error;
  if (!std::filesystem::is_regular_file(m_file, error) || error)
  {
    return;
  }

  std::ifstream in(m_file);
  if (!in)
  {
    Log::warn(LogCategory::editor, "Could not open recent projects file '" + m_file.string() + "'.");
    return;
  }

  nlohmann::json parsed;
  try
  {
    in >> parsed;
  }
  catch (const nlohmann::json::exception& e)
  {
    Log::warn(LogCategory::editor, "Recent projects file '" + m_file.string() + "' is not valid JSON ("
      + e.what() + "); starting with an empty list.");
    return;
  }

  if (!parsed.is_object() || !parsed.contains("projects") || !parsed.at("projects").is_array())
  {
    Log::warn(LogCategory::editor, "Recent projects file '" + m_file.string()
      + "' has an unexpected shape; starting with an empty list.");
    return;
  }

  std::size_t dropped = 0;
  for (const auto& entry : parsed.at("projects"))
  {
    if (!entry.is_object() || !entry.contains("path") || !entry.at("path").is_string())
    {
      ++dropped;
      continue;
    }

    m_projects.push_back(entry);
  }

  if (dropped > 0)
  {
    Log::warn(LogCategory::editor,
      "Dropped " + std::to_string(dropped) + " malformed entries from the recent projects file.");
  }

  parsed.erase("projects");
  m_extra = std::move(parsed);
}

bool RecentProjects::save() const
{
  auto document = m_extra;

  if (const auto it = document.find("version");
      it == document.end() || !it->is_number_integer() || it->get<int>() < currentVersion)
  {
    document["version"] = currentVersion;
  }

  document["projects"] = m_projects;

  std::error_code error;
  if (const auto directory = m_file.parent_path(); !directory.empty())
  {
    std::filesystem::create_directories(directory, error);
    if (error)
    {
      Log::warn(LogCategory::editor, "Could not create the directory for the recent projects file '"
        + directory.string() + "': " + error.message());
      return false;
    }
  }

  std::string serialized;
  try
  {
    serialized = document.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
  }
  catch (const std::exception& e)
  {
    Log::warn(LogCategory::editor, std::string("Could not serialize the recent projects: ") + e.what());
    return false;
  }

  auto temporary = m_file;
  temporary += ".tmp";

  {
    std::ofstream out(temporary, std::ios::trunc);
    out << serialized << '\n';
    out.flush();

    if (!out)
    {
      Log::warn(LogCategory::editor, "Could not write recent projects file '" + temporary.string() + "'.");
      out.close();
      removeQuietly(temporary);
      return false;
    }
  }

  std::filesystem::rename(temporary, m_file, error);
  if (error)
  {
    Log::warn(LogCategory::editor, "Could not move recent projects file '" + temporary.string()
      + "' into place: " + error.message());
    removeQuietly(temporary);
    return false;
  }

  return true;
}

void RecentProjects::recordOpened(const std::filesystem::path& projectPath,
                                  const std::chrono::system_clock::time_point now)
{
  const auto path = normalizedPath(projectPath);
  const auto key = comparisonKey(path);

  nlohmann::json entry = nlohmann::json::object();

  const auto existing = std::find_if(m_projects.begin(), m_projects.end(), [&key](const nlohmann::json& candidate)
  {
    return comparisonKey(candidate.at("path").get<std::string>()) == key;
  });

  if (existing != m_projects.end())
  {
    entry = std::move(*existing);
    m_projects.erase(existing);
  }

  entry["path"] = path;
  entry["name"] = displayName(path);
  entry["lastOpenedUtc"] = formatUtc(now);

  m_projects.insert(m_projects.begin(), std::move(entry));

  evictOverflow();
}

std::vector<RecentProjectEntry> RecentProjects::entries() const
{
  std::vector<RecentProjectEntry> result;
  result.reserve(m_projects.size());

  const auto stringField = [](const nlohmann::json& entry, const char* key)
  {
    const auto it = entry.find(key);
    return it != entry.end() && it->is_string() ? it->get<std::string>() : std::string();
  };

  for (const auto& entry : m_projects)
  {
    result.push_back({
      entry.at("path").get<std::string>(),
      stringField(entry, "name"),
      stringField(entry, "lastOpenedUtc"),
      isPinned(entry),
      stringField(entry, "template")
    });
  }

  return result;
}

const std::filesystem::path& RecentProjects::file() const
{
  return m_file;
}

std::string RecentProjects::formatUtc(const std::chrono::system_clock::time_point time)
{
  const auto seconds = std::chrono::floor<std::chrono::seconds>(time).time_since_epoch().count();

  auto days = seconds / 86400;
  auto secondOfDay = seconds % 86400;
  if (secondOfDay < 0)
  {
    secondOfDay += 86400;
    --days;
  }

  const auto date = civilFromDays(days);

  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), "%04lld-%02u-%02uT%02lld:%02lld:%02lldZ",
                static_cast<long long>(date.year), date.month, date.day,
                static_cast<long long>(secondOfDay / 3600), static_cast<long long>(secondOfDay / 60 % 60),
                static_cast<long long>(secondOfDay % 60));

  return buffer;
}

void RecentProjects::evictOverflow()
{
  while (m_projects.size() > maxEntries)
  {
    // The oldest time wins, and on a tie the later entry, since the list runs newest first.
    auto victim = m_projects.end();
    std::int64_t victimTime = 0;

    for (auto it = m_projects.begin(); it != m_projects.end(); ++it)
    {
      if (isPinned(*it))
      {
        continue;
      }

      const auto time = parseUtc(*it);
      if (victim == m_projects.end() || time <= victimTime)
      {
        victim = it;
        victimTime = time;
      }
    }

    if (victim == m_projects.end())
    {
      return;
    }

    m_projects.erase(victim);
  }
}

bool recordProjectOpened(const std::filesystem::path& projectPath)
{
  RecentProjects recent(RecentProjects::defaultFile());
  recent.load();
  recent.recordOpened(projectPath, std::chrono::system_clock::now());

  return recent.save();
}
