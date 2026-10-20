#include "RecentProjects.h"
#include <Log.h>
#include <UserDataDirectory.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <optional>
#include <random>
#include <string_view>
#include <thread>
#include <utility>

namespace {
  constexpr int currentVersion = 1;
  constexpr int renameAttempts = 5;
  constexpr std::chrono::milliseconds renameRetryDelay{ 20 };

  // Paths are held as UTF-8 text: the narrow native encoding on Windows is the ANSI code page.
  std::string toUtf8(const std::filesystem::path& path)
  {
    const auto text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
  }

  std::filesystem::path fromUtf8(const std::string& text)
  {
    const auto* first = reinterpret_cast<const char8_t*>(text.data());
    return std::filesystem::path(first, first + text.size());
  }

  std::string asciiLower(std::string text)
  {
    for (auto& c : text)
    {
      if (c >= 'A' && c <= 'Z')
      {
        c = static_cast<char>(c - 'A' + 'a');
      }
    }

    return text;
  }

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

  bool matchesTimeShape(const std::string& text)
  {
    constexpr std::string_view shape = "dddd-dd-ddTdd:dd:ddZ";

    if (text.size() != shape.size())
    {
      return false;
    }

    for (std::size_t i = 0; i < shape.size(); ++i)
    {
      const bool matches = shape[i] == 'd' ? (text[i] >= '0' && text[i] <= '9') : text[i] == shape[i];
      if (!matches)
      {
        return false;
      }
    }

    return true;
  }

  int readNumber(const std::string& text, const std::size_t position, const std::size_t length)
  {
    int value = 0;
    for (std::size_t i = 0; i < length; ++i)
    {
      value = value * 10 + (text[position + i] - '0');
    }

    return value;
  }

  int daysInMonth(const int year, const int month)
  {
    static constexpr int lengths[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;

    return month == 2 && leap ? 29 : lengths[month - 1];
  }

  // Only the exact written format parses; anything else is nullopt.
  std::optional<std::int64_t> parseUtc(const std::string& text)
  {
    if (!matchesTimeShape(text))
    {
      return std::nullopt;
    }

    const int year = readNumber(text, 0, 4);
    const int month = readNumber(text, 5, 2);
    const int day = readNumber(text, 8, 2);
    const int hour = readNumber(text, 11, 2);
    const int minute = readNumber(text, 14, 2);
    const int second = readNumber(text, 17, 2);

    if (month < 1 || month > 12 || day < 1 || day > daysInMonth(year, month) || hour > 23 || minute > 59
        || second > 59)
    {
      return std::nullopt;
    }

    return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400
           + hour * 3600 + minute * 60 + second;
  }

  std::string stringField(const nlohmann::json& entry, const char* key)
  {
    const auto it = entry.find(key);
    return it != entry.end() && it->is_string() ? it->get<std::string>() : std::string();
  }

  // A missing or unparseable time sorts as the oldest.
  std::int64_t entryTime(const nlohmann::json& entry)
  {
    return parseUtc(stringField(entry, "lastOpenedUtc")).value_or(std::numeric_limits<std::int64_t>::min());
  }

  bool isPinned(const nlohmann::json& entry)
  {
    const auto it = entry.find("pinned");
    return it != entry.end() && it->is_boolean() && it->get<bool>();
  }

  std::string comparisonKey(std::string path)
  {
#if defined(_WIN32) || defined(__APPLE__)
    path = asciiLower(std::move(path));
#endif
    return path;
  }

  // The form a project is recorded in: absolute, generic separators, no trailing separator except on a
  // root. A project file under a folder (project.ecs3d) is recorded as the folder.
  std::string canonicalPath(const std::string& text)
  {
    std::error_code error;
    auto absolute = std::filesystem::absolute(fromUtf8(text), error);
    if (error)
    {
      absolute = fromUtf8(text);
    }

    absolute = absolute.lexically_normal();

    if (asciiLower(toUtf8(absolute.extension())) == ".ecs3d")
    {
      absolute = absolute.parent_path();
    }

    auto result = toUtf8(absolute);
    const auto rootLength = toUtf8(absolute.root_path()).size();
    while (result.size() > rootLength && result.size() > 1 && result.back() == '/')
    {
      result.pop_back();
    }

    return result;
  }

  std::string displayName(const std::string& canonical)
  {
    const auto path = fromUtf8(canonical);

    if (asciiLower(toUtf8(path.extension())) == ".json")
    {
      return toUtf8(path.stem());
    }

    return toUtf8(path.filename());
  }

  void removeQuietly(const std::filesystem::path& path)
  {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }

  std::optional<nlohmann::json> readDocument(const std::filesystem::path& file)
  {
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error) || error)
    {
      return std::nullopt;
    }

    const auto name = toUtf8(file);

    std::ifstream in(file);
    if (!in)
    {
      Log::warn(LogCategory::editor, "Could not open recent projects file '" + name + "'.");
      return std::nullopt;
    }

    nlohmann::json parsed;
    try
    {
      in >> parsed;
    }
    catch (const nlohmann::json::exception& e)
    {
      Log::warn(LogCategory::editor, "Recent projects file '" + name + "' is not valid JSON ("
        + e.what() + "); starting with an empty list.");
      return std::nullopt;
    }

    if (!parsed.is_object() || (parsed.contains("projects") && !parsed.at("projects").is_array()))
    {
      Log::warn(LogCategory::editor,
        "Recent projects file '" + name + "' has an unexpected shape; starting with an empty list.");
      return std::nullopt;
    }

    return parsed;
  }

  std::vector<nlohmann::json> validEntries(const nlohmann::json& projects)
  {
    std::vector<nlohmann::json> result;
    std::size_t dropped = 0;

    for (const auto& entry : projects)
    {
      if (entry.is_object() && entry.contains("path") && entry.at("path").is_string())
      {
        result.push_back(entry);
      }
      else
      {
        ++dropped;
      }
    }

    if (dropped > 0)
    {
      Log::warn(LogCategory::editor,
        "Dropped " + std::to_string(dropped) + " malformed entries from the recent projects file.");
    }

    return result;
  }

  std::optional<std::string> serializeDocument(nlohmann::json document,
                                               const std::vector<nlohmann::json>& projects)
  {
    if (const auto it = document.find("version");
        it == document.end() || !it->is_number_integer() || it->get<int>() < currentVersion)
    {
      document["version"] = currentVersion;
    }

    document["projects"] = projects;

    try
    {
      return document.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    catch (const std::exception& e)
    {
      Log::warn(LogCategory::editor, std::string("Could not serialize the recent projects: ") + e.what());
      return std::nullopt;
    }
  }

  bool ensureDirectory(const std::filesystem::path& file)
  {
    const auto directory = file.parent_path();
    if (directory.empty())
    {
      return true;
    }

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
    {
      Log::warn(LogCategory::editor, "Could not create the directory for the recent projects file '"
        + toUtf8(directory) + "': " + error.message());
      return false;
    }

    return true;
  }

  // Unique per writer, so the editor and the launcher never interleave on one temporary file.
  std::filesystem::path uniqueTemporary(const std::filesystem::path& file)
  {
    std::random_device device;
    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "%08x", static_cast<unsigned>(device()));

    auto temporary = file;
    temporary += ".";
    temporary += suffix;
    temporary += ".tmp";

    return temporary;
  }

  bool writeContents(const std::filesystem::path& file, const std::string& contents)
  {
    std::ofstream out(file, std::ios::trunc);
    out << contents << '\n';
    out.flush();

    return static_cast<bool>(out);
  }

  // The destination can be held open for a moment by the other program that reads this file.
  bool renameWithRetry(const std::filesystem::path& from, const std::filesystem::path& to)
  {
    std::error_code error;

    for (int attempt = 0; attempt < renameAttempts; ++attempt)
    {
      if (attempt > 0)
      {
        std::this_thread::sleep_for(renameRetryDelay);
      }

      std::filesystem::rename(from, to, error);
      if (!error)
      {
        return true;
      }
    }

    Log::warn(LogCategory::editor, "Could not move recent projects file '" + toUtf8(from)
      + "' into place: " + error.message());
    return false;
  }

  // Removes every entry for the key and returns the first of them, or a new empty entry.
  nlohmann::json takeMatching(std::vector<nlohmann::json>& projects, const std::string& key)
  {
    std::optional<nlohmann::json> first;

    for (auto it = projects.begin(); it != projects.end();)
    {
      if (comparisonKey(canonicalPath(it->at("path").get<std::string>())) != key)
      {
        ++it;
        continue;
      }

      if (!first)
      {
        first = std::move(*it);
      }

      it = projects.erase(it);
    }

    return first ? *first : nlohmann::json::object();
  }

  // The front entry is the one just recorded, so it is never a candidate. The oldest wins, and on a tie
  // the later entry, since the list runs newest first.
  std::vector<nlohmann::json>::iterator findVictim(std::vector<nlohmann::json>& projects)
  {
    auto victim = projects.end();
    std::int64_t victimTime = 0;

    for (auto it = projects.begin() + 1; it != projects.end(); ++it)
    {
      if (isPinned(*it))
      {
        continue;
      }

      const auto time = entryTime(*it);
      if (victim == projects.end() || time <= victimTime)
      {
        victim = it;
        victimTime = time;
      }
    }

    return victim;
  }

  void evictOverflow(std::vector<nlohmann::json>& projects)
  {
    while (projects.size() > RecentProjects::maxEntries)
    {
      const auto victim = findVictim(projects);
      if (victim == projects.end())
      {
        return;
      }

      projects.erase(victim);
    }
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

  auto document = readDocument(m_file);
  if (!document)
  {
    return;
  }

  if (const auto it = document->find("projects"); it != document->end())
  {
    m_projects = validEntries(*it);
    document->erase(it);
  }

  m_extra = std::move(*document);
}

bool RecentProjects::save() const
{
  const auto serialized = serializeDocument(m_extra, m_projects);
  if (!serialized || !ensureDirectory(m_file))
  {
    return false;
  }

  const auto temporary = uniqueTemporary(m_file);

  if (!writeContents(temporary, *serialized))
  {
    Log::warn(LogCategory::editor, "Could not write recent projects file '" + toUtf8(temporary) + "'.");
    removeQuietly(temporary);
    return false;
  }

  if (!renameWithRetry(temporary, m_file))
  {
    removeQuietly(temporary);
    return false;
  }

  return true;
}

void RecentProjects::recordOpened(const std::filesystem::path& projectPath,
                                  const std::chrono::system_clock::time_point now)
{
  const auto path = canonicalPath(toUtf8(projectPath));

  auto entry = takeMatching(m_projects, comparisonKey(path));
  entry["path"] = path;
  entry["name"] = displayName(path);
  entry["lastOpenedUtc"] = formatUtc(now);

  m_projects.insert(m_projects.begin(), std::move(entry));

  evictOverflow(m_projects);
}

std::vector<RecentProjectEntry> RecentProjects::entries() const
{
  std::vector<RecentProjectEntry> result;
  result.reserve(m_projects.size());

  for (const auto& entry : m_projects)
  {
    result.push_back({
      stringField(entry, "path"),
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

bool recordProjectOpened(const std::filesystem::path& projectPath)
{
  RecentProjects recent(RecentProjects::defaultFile());
  recent.load();
  recent.recordOpened(projectPath, std::chrono::system_clock::now());

  return recent.save();
}
