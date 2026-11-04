#ifndef PROFILERPANEL_H
#define PROFILERPANEL_H

#include "ProfilerPanelState.h"
#include "ProfilerServerLane.h"
#include <History.h>
#include <optional>
#include <vector>

class SettingsStore;

namespace profile {
  class Lane;
}

// The editor's "Profiler" window: the editor's own frame lane and the server's tick lane as stacked bars with legends,
// a per-scope breakdown and the Network section.
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

  // The server's figures for this frame. The pointer is read during displayGui only, so it need only outlive that call.
  void setServerLane(const ServerLaneInput* input);

  [[nodiscard]] int windowSeconds() const;

private:
  const profile::Lane& m_lane;
  SettingsStore& m_settings;

  bool m_open = false;
  bool m_focusRequested = false;

  int m_windowSeconds;
  double m_scaleMillis;
  uint32_t m_hiddenMask;
  uint32_t m_serverHiddenMask;

  const ServerLaneInput* m_serverInput = nullptr;

  profilerPanelState::Interaction m_interaction;

  // A copy taken when pausing, so the frozen view neither moves nor loses its selection as the ring wraps.
  std::optional<profile::History> m_frozen;
  std::optional<profile::History> m_frozenServer;
  std::vector<profile::ViewNode> m_frozenServerNodes;
  std::vector<uint32_t> m_frozenServerTicks;

  void displayHeader();

  void displayLane(const profile::History& history, const std::vector<profile::ViewNode>& nodes);

  void displayServerLane(const std::vector<profile::ViewNode>& nodes);

  void displayBreakdown(const profile::History& history, const std::vector<profile::ViewNode>& nodes,
                        const std::vector<profile::ViewNode>& serverNodes);

  void displayNetwork() const;

  void freezeServer();
};

#endif
