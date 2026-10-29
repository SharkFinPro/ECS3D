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

// A dedicated server runs until a shutdown is requested. An exitWhenEmpty (editor/client-spawned) server also
// stops once its last connection drops, but only after the first connect, so it survives the launch -> connect
// window when the count is still 0.
[[nodiscard]] constexpr bool shouldKeepRunning(const bool shutdownRequested, const bool exitWhenEmpty,
                                               const bool hasConnected, const int connectionCount) noexcept
{
  if (shutdownRequested)
  {
    return false;
  }

  return !exitWhenEmpty || !hasConnected || connectionCount > 0;
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
// instances, and pausing or stopping a stopped scene does nothing. loadScene is handled before this is consulted.
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
      plan.pauseScene = !wasStopped;
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

// What the server does with a join. A connection joins once; answering a repeat would rebuild and resend the
// whole project on the tick thread, so a repeat is dropped and logged once per connection.
struct JoinPlan
{
  bool answer = false;
  bool warnRepeat = false;

  [[nodiscard]] constexpr bool operator==(const JoinPlan&) const noexcept = default;
};

[[nodiscard]] constexpr JoinPlan planJoin(const bool alreadyJoined, const bool repeatWarned) noexcept
{
  if (!alreadyJoined)
  {
    return {true, false};
  }

  return {false, !repeatWarned};
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

// One tick's network drain. The dropped connections are taken before the inbox is drained and handled after it, so
// every message a connection sent before its disconnect was queued is handled while that connection still exists;
// a disconnect queued during the drain waits for the next tick, after that tick's drain. Handling it first would
// let a late join or input bind a player slot to a connection id that is already gone and never freed.
template <typename Message, typename TakeDisconnected, typename Poll, typename OnMessage, typename OnDisconnect>
void drainInboxThenDisconnects(TakeDisconnected&& takeDisconnected, Poll&& poll, OnMessage&& onMessage,
                               OnDisconnect&& onDisconnect)
{
  const auto disconnected = takeDisconnected();

  Message message;
  int32_t senderId = 0;
  while (poll(message, senderId))
  {
    onMessage(message, senderId);
  }

  for (const auto connId : disconnected)
  {
    onDisconnect(connId);
  }
}

#endif //SERVERPOLICY_H
