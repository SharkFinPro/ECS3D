#ifndef SETTINGSSTORE_H
#define SETTINGSSTORE_H

#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <string>

// Per-user, per-machine editor preferences, held outside project data on purpose: project state is
// authoritative and replicated to every connected client as a snapshot, so a preference stored there
// would travel on the wire and be shared between users.
//
// Every key is read independently against a compiled-in default, and keys this build does not know are
// carried through untouched, so a file written by a newer build survives an older one.
class SettingsStore {
public:
  // %APPDATA%/ECS3D on Windows, $XDG_CONFIG_HOME (or ~/.config) /ECS3D on Linux,
  // ~/Library/Application Support/ECS3D on macOS. The editor and the launcher own separate files here
  // and neither reads the other's.
  [[nodiscard]] static std::filesystem::path defaultFile();

  explicit SettingsStore(std::filesystem::path file);

  ~SettingsStore();

  SettingsStore(const SettingsStore&) = delete;
  SettingsStore& operator=(const SettingsStore&) = delete;

  // Reads the file if it is there. A file that fails to parse is renamed aside rather than overwritten,
  // and the defaults are used, so the editor always starts.
  void load();

  template <typename T>
  [[nodiscard]] T get(const std::string& key, const T& fallback) const
  {
    const auto it = m_values.find(key);

    if (it == m_values.end())
    {
      return fallback;
    }

    // A key of the wrong type is treated like an absent one rather than throwing into the caller.
    try
    {
      return it->get<T>();
    }
    catch (const nlohmann::json::exception&)
    {
      return fallback;
    }
  }

  template <typename T>
  void set(const std::string& key, const T& value)
  {
    if (const auto it = m_values.find(key); it != m_values.end() && *it == nlohmann::json(value))
    {
      return;
    }

    m_values[key] = value;

    scheduleWrite();
  }

  // Drives the debounce; call once per frame. Rapid edits collapse into a single write.
  void update();

  // Writes immediately if anything is pending.
  void flush();

  [[nodiscard]] const std::filesystem::path& file() const;

  [[nodiscard]] bool hasPendingWrite() const;

private:
  static constexpr int settingsVersion = 1;

  // Long enough that dragging a slider settles into one write, short enough to survive a hard kill.
  static constexpr std::chrono::milliseconds writeDelay{ 1000 };

  std::filesystem::path m_file;

  nlohmann::json m_values = nlohmann::json::object();

  bool m_writePending = false;
  std::chrono::steady_clock::time_point m_writeDue;

  void scheduleWrite();

  void write();

  // Moves an unparseable file to <name>.bad so it is never silently overwritten.
  void setAside() const;
};



#endif //SETTINGSSTORE_H
