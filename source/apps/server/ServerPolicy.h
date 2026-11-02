#ifndef SERVERPOLICY_H
#define SERVERPOLICY_H

#include <Protocol.h>
#include <Replication.h>
#include <RemoteLogSink.h>
#include <ServerLog.h>
#include <scenes/SceneManager.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

// A loop iteration that ran a step closes its profiling sample; one that did not leaves it open so idle spins fold
// into the next, until the sample is a whole fixed step old.
[[nodiscard]] constexpr bool shouldCloseServerSample(const bool ticked, const double openSeconds,
                                                     const double fixedDt) noexcept
{
  return ticked || openSeconds >= fixedDt;
}

// Whole fixed steps in the time FixedTimestep::advance discarded.
[[nodiscard]] inline int droppedStepCount(const float droppedSeconds, const float fixedDt)
{
  return droppedSeconds >= fixedDt ? static_cast<int>(std::floor(droppedSeconds / fixedDt)) : 0;
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

// A half-written component has no delta stream to correct it for most types, so the only way back
// to agreement is a fresh snapshot.
[[nodiscard]] constexpr bool componentEditRefusalNeedsResync(const replication::ComponentEditResult result) noexcept
{
  return result == replication::ComponentEditResult::partiallyApplied;
}

// A refusal the sender could have predicted needs no snapshot: rejected means the authority and the sender agree
// about the scene and the op simply changes nothing, and a malformed edit is a payload problem that resending the
// scene would not fix - and is the one a client can send on demand. The exception is a creating op refused for
// the uuid it chose: the sender gets neither the object nor an error, so it has to notice the missing object
// itself rather than being resynced here.
//
// Everything else means the sender's view disagrees with the authority: an object or component it believes exists
// and does not, a prefab it cannot resolve, an edit that threw part way through. Those are exactly the cases a
// snapshot repairs, and there is no client-initiated resync to fall back on - a structural edit was the only thing
// that rebuilt a drifted view.
[[nodiscard]] constexpr bool sceneEditRefusalNeedsResync(const replication::SceneEditResult result) noexcept
{
  switch (result)
  {
    case replication::SceneEditResult::rejected:
    case replication::SceneEditResult::malformedEdit:
      return false;

    case replication::SceneEditResult::applied:
    case replication::SceneEditResult::unknownObject:
    case replication::SceneEditResult::unknownComponent:
    case replication::SceneEditResult::unknownAsset:
    case replication::SceneEditResult::failed:
      return true;
  }

  return true;
}

// Caps what one forward can hand to the network, so a log storm costs bounded work per loop iteration instead of
// one send sized by however much piled up; anything past either cap is picked up on the next iteration (or evicted
// by RemoteLogSink's own capacity first, and counted in the batch's dropped count). The byte cap stays well under
// TransportBackend::MaxMessageBytes (64 MiB) - RemoteLogSink::drain's estimate is rough, and a batch that actually
// hit that ceiling would be refused and lost outright rather than sent short.
inline constexpr std::size_t maxLogEntriesPerBatch = 200;
inline constexpr std::size_t maxLogBatchBytes = 256 * 1024;

// Drains one capped batch and hands it to send as a packed serverLog message; sends nothing when the drain has no
// entries and no evictions to report.
template <typename Send>
void forwardDrainedLog(RemoteLogSink& sink, Send&& send)
{
  const auto drained = sink.drain(maxLogEntriesPerBatch, maxLogBatchBytes);
  if (drained.entries.empty() && drained.dropped == 0)
  {
    return;
  }

  send(net::packServerLog(drained.entries, drained.dropped));
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
