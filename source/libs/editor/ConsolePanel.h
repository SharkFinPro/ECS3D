#ifndef CONSOLEPANEL_H
#define CONSOLEPANEL_H

#include <LogEntry.h>
#include <LogFilter.h>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class RingBufferSink;

// The editor's "Console" window: everything Log::write sends to the shared RingBufferSink, with per-level
// and per-category toggles, a text search, copy-to-clipboard, and a clear that only hides history (the
// sink is shared, so clearing here must not affect anything else reading it).
class ConsolePanel {
public:
  explicit ConsolePanel(std::shared_ptr<RingBufferSink> sink);

  void displayGui();

  // Shows the panel and asks ImGui to focus it, matching SettingsPanel's Window-menu behavior.
  void open();

  [[nodiscard]] bool isOpen() const;

  void setOpen(bool open);

private:
  std::shared_ptr<RingBufferSink> m_sink;

  bool m_open = true;
  bool m_focusRequested = false;

  LogFilter m_filter;
  char m_search[128] = {};

  // Entries at or before this sink sequence number are hidden - "Clear" without mutating the shared sink.
  std::uint64_t m_clearedBefore = 0;

  bool m_autoScroll = true;

  // Cache of the current filtered view, rebuilt only when the sink has new entries or the filter changed.
  bool m_cacheDirty = true;
  std::uint64_t m_lastSeenSequence = 0;
  bool m_sawNewEntries = false;
  std::vector<LogEntry> m_filtered;
  std::array<int, levelCount> m_levelCounts{};

  void refreshCacheIfNeeded();

  void displayFilterRow();

  void displayActionRow();

  void displayRows();
};

#endif  // CONSOLEPANEL_H
