#include "SettingsStore.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <utility>

namespace {
  // Absent or empty is the interesting case: getenv returns null on Windows for an unset variable and
  // an empty string is just as useless as one.
  const char* environmentPath(const char* name)
  {
    const char* value = std::getenv(name);

    return value && *value ? value : nullptr;
  }
}

std::filesystem::path SettingsStore::defaultFile()
{
  std::filesystem::path directory;

#ifdef _WIN32
  if (const char* appData = environmentPath("APPDATA"))
  {
    directory = std::filesystem::path(appData) / "ECS3D";
  }
#elif defined(__APPLE__)
  if (const char* home = environmentPath("HOME"))
  {
    directory = std::filesystem::path(home) / "Library" / "Application Support" / "ECS3D";
  }
#else
  if (const char* configHome = environmentPath("XDG_CONFIG_HOME"))
  {
    directory = std::filesystem::path(configHome) / "ECS3D";
  }
  else if (const char* home = environmentPath("HOME"))
  {
    directory = std::filesystem::path(home) / ".config" / "ECS3D";
  }
#endif

  // With no home directory to resolve against, a path under the working directory still lets the editor
  // start and persist settings.
  if (directory.empty())
  {
    directory = std::filesystem::path("ECS3D");
  }

  return directory / "settings.json";
}

SettingsStore::SettingsStore(std::filesystem::path file, const std::chrono::milliseconds writeDelay)
  : m_file(std::move(file)), m_writeDelay(writeDelay)
{
  load();
}

SettingsStore::~SettingsStore()
{
  try
  {
    flush();
  }
  catch (...)
  {
  }
}

void SettingsStore::load()
{
  m_values = nlohmann::json::object();
  m_writePending = false;

  std::error_code error;
  if (!std::filesystem::is_regular_file(m_file, error) || error)
  {
    return;
  }

  std::ifstream in(m_file);
  if (!in)
  {
    std::cerr << "[SettingsStore] Could not open '" << m_file.string() << "'; using defaults." << std::endl;
    return;
  }

  nlohmann::json parsed;
  try
  {
    in >> parsed;
  }
  catch (const nlohmann::json::exception& e)
  {
    std::cerr << "[SettingsStore] '" << m_file.string() << "' is not valid JSON (" << e.what()
              << "); using defaults." << std::endl;
    in.close();
    setAside();
    return;
  }

  if (!parsed.is_object())
  {
    std::cerr << "[SettingsStore] '" << m_file.string() << "' is not a JSON object; using defaults." << std::endl;
    in.close();
    setAside();
    return;
  }

  m_values = std::move(parsed);
}

void SettingsStore::update()
{
  if (!m_writePending)
  {
    return;
  }

  const auto now = std::chrono::steady_clock::now();

  if (now >= m_writeDue || now - m_writeFirstRequested >= maxWriteDelay)
  {
    write();
  }
}

void SettingsStore::flush()
{
  if (m_writePending)
  {
    write();
  }
}

const std::filesystem::path& SettingsStore::file() const
{
  return m_file;
}

bool SettingsStore::hasPendingWrite() const
{
  return m_writePending;
}

void SettingsStore::scheduleWrite()
{
  if (!m_writePending)
  {
    m_writeFirstRequested = std::chrono::steady_clock::now();
  }

  m_writePending = true;
  m_writeDue = std::chrono::steady_clock::now() + m_writeDelay;
}

void SettingsStore::write()
{
  m_writePending = false;

  // Only ever raise it: a file written by a newer build keeps its own version, or the migration anchor
  // this field exists to be would be destroyed by whichever build happened to write last.
  if (const auto it = m_values.find("version");
      it == m_values.end() || !it->is_number_integer() || it->get<int>() < settingsVersion)
  {
    m_values["version"] = settingsVersion;
  }

  std::error_code error;
  if (const auto directory = m_file.parent_path(); !directory.empty())
  {
    std::filesystem::create_directories(directory, error);
    if (error)
    {
      std::cerr << "[SettingsStore] Could not create '" << directory.string() << "': " << error.message()
                << std::endl;
      scheduleWrite();
      return;
    }
  }

  std::string serialized;
  try
  {
    // replace rather than strict: a preference holding a path in the native narrow encoding need not be
    // valid UTF-8, and losing every setting to one bad byte is worse than mangling that one string.
    serialized = m_values.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
  }
  catch (const std::exception& e)
  {
    std::cerr << "[SettingsStore] Could not serialize settings: " << e.what() << std::endl;
    scheduleWrite();
    return;
  }

  // Written beside the real file and moved into place, so a crash mid-write cannot leave the known-good
  // settings truncated - which matters, since the last write happens during shutdown.
  auto temporary = m_file;
  temporary += ".tmp";

  {
    std::ofstream out(temporary, std::ios::trunc);
    if (!out)
    {
      std::cerr << "[SettingsStore] Could not write '" << temporary.string() << "'." << std::endl;
      scheduleWrite();
      return;
    }

    out << serialized << std::endl;
  }

  std::filesystem::rename(temporary, m_file, error);
  if (error)
  {
    std::cerr << "[SettingsStore] Could not move '" << temporary.string() << "' into place: "
              << error.message() << std::endl;
    std::filesystem::remove(temporary, error);
    scheduleWrite();
  }
}

void SettingsStore::setAside() const
{
  auto spoiled = m_file;
  spoiled += ".bad";

  // rename replaces an existing destination on every platform we build for, so an older .bad is only
  // discarded once its replacement is safely in place.
  std::error_code error;
  std::filesystem::rename(m_file, spoiled, error);

  if (error)
  {
    std::cerr << "[SettingsStore] Could not move '" << m_file.string() << "' aside: " << error.message()
              << std::endl;
    return;
  }

  std::cerr << "[SettingsStore] Moved the unreadable settings file to '" << spoiled.string() << "'."
            << std::endl;
}
