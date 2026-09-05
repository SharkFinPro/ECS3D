#ifndef SETTINGSSTORE_H
#define SETTINGSSTORE_H

#include <nlohmann/json.hpp>
#include <chrono>
#include <exception>
#include <filesystem>
#include <string>

// Per-user, per-machine editor preferences, held outside project data on purpose: project state is
// authoritative and replicated to every connected client as a snapshot, so a preference stored there
// would travel on the wire and be shared between users.
//
// Every key is read independently against a compiled-in default, and keys this build does not know are
// carried through untouched, so a file written by a newer build survives an older one. "version" is
// reserved by the store itself.
//
// Not thread safe: it belongs to the app loop that calls update().
class SettingsStore {
public:
  // %APPDATA%/ECS3D on Windows, $XDG_CONFIG_HOME (or ~/.config) /ECS3D on Linux,
  // ~/Library/Application Support/ECS3D on macOS.
  [[nodiscard]] static std::filesystem::path defaultFile();

  // Reads the file if it is there, so a store is never half-initialized: writing without having read
  // first would drop every key this build does not set. A file that fails to parse is renamed aside
  // rather than overwritten, and the defaults are used, so the editor always starts.
  explicit SettingsStore(std::filesystem::path file,
                         std::chrono::milliseconds writeDelay = std::chrono::milliseconds(1000));

  ~SettingsStore();

  SettingsStore(const SettingsStore&) = delete;
  SettingsStore& operator=(const SettingsStore&) = delete;

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
    catch (const std::exception&)
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

  // A resetting debounce alone would never write while something changes every frame, so a pending
  // write is forced through once it has been waiting this long however busy the caller is.
  static constexpr std::chrono::milliseconds maxWriteDelay{ 5000 };

  std::filesystem::path m_file;

  std::chrono::milliseconds m_writeDelay;

  nlohmann::json m_values = nlohmann::json::object();

  bool m_writePending = false;
  std::chrono::steady_clock::time_point m_writeDue;
  std::chrono::steady_clock::time_point m_writeFirstRequested;

  void load();

  void scheduleWrite();

  void write();

  // Moves an unparseable file to <name>.bad so it is never silently overwritten.
  void setAside() const;
};



#endif //SETTINGSSTORE_H
