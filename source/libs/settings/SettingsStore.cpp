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

  // With no home directory to resolve against, keeping settings beside the executable still lets the
  // editor start and persist them.
  if (directory.empty())
  {
    directory = std::filesystem::path("ECS3D");
  }

  return directory / "settings.json";
}

SettingsStore::SettingsStore(std::filesystem::path file)
  : m_file(std::move(file))
{}

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
  if (!exists(m_file, error) || error)
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
  if (m_writePending && std::chrono::steady_clock::now() >= m_writeDue)
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
  m_writePending = true;
  m_writeDue = std::chrono::steady_clock::now() + writeDelay;
}

void SettingsStore::write()
{
  m_writePending = false;

  // Nothing reads the version yet; it is written from the first release so a future breaking change has
  // an anchor to migrate from.
  m_values["version"] = settingsVersion;

  std::error_code error;
  if (const auto directory = m_file.parent_path(); !directory.empty())
  {
    create_directories(directory, error);
    if (error)
    {
      std::cerr << "[SettingsStore] Could not create '" << directory.string() << "': " << error.message()
                << std::endl;
      return;
    }
  }

  std::ofstream out(m_file, std::ios::trunc);
  if (!out)
  {
    std::cerr << "[SettingsStore] Could not write '" << m_file.string() << "'." << std::endl;
    return;
  }

  out << m_values.dump(2) << std::endl;
}

void SettingsStore::setAside() const
{
  auto spoiled = m_file;
  spoiled += ".bad";

  std::error_code error;
  remove(spoiled, error);
  rename(m_file, spoiled, error);

  if (error)
  {
    std::cerr << "[SettingsStore] Could not move '" << m_file.string() << "' aside: " << error.message()
              << std::endl;
    return;
  }

  std::cerr << "[SettingsStore] Moved the unreadable settings file to '" << spoiled.string() << "'."
            << std::endl;
}
