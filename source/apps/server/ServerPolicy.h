#ifndef SERVERPOLICY_H
#define SERVERPOLICY_H

#include <Protocol.h>
#include <scenes/SceneManager.h>
#include <string>

// ServerApp cannot be built into the headless test suite (its constructor boots the CLR), so the pure
// decisions it makes live here.

// A mutation passes only on an edit-mode server and only from a connection the transport authorized as an
// editor; anything else is always allowed through.
[[nodiscard]] constexpr bool isMutationAuthorized(const net::MessageType type, const bool editMode,
                                                  const bool connectionIsEditor) noexcept
{
  return !net::isMutationMessage(type) || (editMode && connectionIsEditor);
}

// A play server admits an editor connection without a token, so forwarding its log there would hand the log
// to anyone who claims the role. Only an edit-mode server, where the token check applies, forwards it.
[[nodiscard]] constexpr bool forwardsLogToEditors(const bool editMode) noexcept
{
  return editMode;
}

// Hot reload is polled only where fixedUpdate never runs: an edit-mode server whose scene is stopped. A play
// server auto-starts, and a paused scene keeps its started instances.
[[nodiscard]] constexpr bool shouldPollScriptsWhileStopped(const bool editMode, const bool hasScene,
                                                           const SceneStatus status) noexcept
{
  return editMode && hasScene && status == SceneStatus::stopped;
}

// Nobody can see or affect a scene while no one is connected, so by default the server stops ticking it rather
// than burning CPU on a simulation no one observes. --simulate-when-empty keeps a dedicated server's world running.
[[nodiscard]] constexpr bool shouldSimulate(const int connectionCount, const bool simulateWhenEmpty) noexcept
{
  return simulateWhenEmpty || connectionCount > 0;
}

// What a scene-control op does. The order the steps run in lives in ServerApp::applySceneControl.
struct SceneControlPlan
{
  bool startScene = false;
  bool pauseScene = false;
  bool resetScene = false;
  bool startScripts = false;
  bool stopScripts = false;
  bool resetCollisions = false;

  [[nodiscard]] constexpr bool operator==(const SceneControlPlan&) const noexcept = default;
};

// Scripts and contact history are only touched on a real transition: a resume from pause keeps the live
// instances, and stopping a stopped scene does nothing. loadScene is handled before this is consulted.
[[nodiscard]] constexpr SceneControlPlan planSceneControl(const net::SceneControlOp op, const bool wasStopped) noexcept
{
  SceneControlPlan plan;

  switch (op)
  {
    case net::SceneControlOp::start:
      plan.startScene = true;
      plan.startScripts = wasStopped;
      plan.resetCollisions = wasStopped;
      break;

    case net::SceneControlOp::pause:
      plan.pauseScene = true;
      break;

    case net::SceneControlOp::stop:
      plan.stopScripts = !wasStopped;
      plan.resetScene = !wasStopped;
      plan.resetCollisions = !wasStopped;
      break;

    case net::SceneControlOp::loadScene:
      break;
  }

  return plan;
}

// An edit server with no token would authorize any connection that claims the editor role, so one without a
// token gets a generated one, unless it was explicitly launched open for development.
[[nodiscard]] inline bool needsGeneratedEditToken(const bool editMode, const bool openEdit, const std::string& token)
{
  return editMode && !openEdit && token.empty();
}

// An open edit server is only actually open when no token was given with it.
[[nodiscard]] inline bool needsOpenEditWarning(const bool openEdit, const std::string& token)
{
  return openEdit && token.empty();
}

#endif //SERVERPOLICY_H
