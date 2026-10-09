#ifndef SERVERPOLICY_H
#define SERVERPOLICY_H

#include <Protocol.h>

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

#endif //SERVERPOLICY_H
