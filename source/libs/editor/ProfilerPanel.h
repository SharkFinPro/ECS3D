#ifndef PROFILERPANEL_H
#define PROFILERPANEL_H

#include "ProfilerPanelState.h"
#include <History.h>
#include <optional>

class SettingsStore;

namespace profile {
  class Lane;
}

// The editor's "Profiler" window: the editor's own frame lane as stacked bars, a legend and a per-scope breakdown.
// The lane must be read on the thread that records it, so displayGui is called from that thread.
class ProfilerPanel {
public:
  ProfilerPanel(const profile::Lane& lane, SettingsStore& settings);

  void displayGui();

  // Shows the panel and asks ImGui to focus it, matching the other panels' Window-menu behavior.
  void open();

  [[nodiscard]] bool isOpen() const;

  void setOpen(bool open);

  void toggle();

private:
  const profile::Lane& m_lane;
  SettingsStore& m_settings;

  bool m_open = false;
  bool m_focusRequested = false;

  int m_windowSeconds;
  double m_scaleMillis;
  uint32_t m_hiddenMask;

  profilerPanelState::Interaction m_interaction;

  // A copy taken when pausing, so the frozen view neither moves nor loses its selection as the ring wraps.
  std::optional<profile::History> m_frozen;

  void displayHeader();

  void displayLane(const profile::History& history);
};

#endif
