#include "ServerApp.h"
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <objects/Object.h>
#include <objects/components/Component.h>
#include <CollisionSystem.h>
#include <ScriptSystem.h>
#include <bindings/InputState.h>
#include <NetServer.h>
#include <Log.h>
#include <Replication.h>
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

void ServerApp::handleClientMessage(const net::Message& message, const int32_t senderId)
{
  // A non-edit server is read-only: it serves snapshots/deltas to editors that connect to view it, but
  // never applies their edits. On an edit-mode server, a mutation is honored only from the connection the
  // transport authorized as Role::editor at the handshake - a connection that simply claims Role::player
  // (which needs no token) must not be able to reach the same handlers.
  if (!isMessageAuthorized(message, senderId))
  {
    Log::error(LogCategory::server, "Discarded a message of type " + std::to_string(static_cast<int>(message.getType())) +
                        " from connection " + std::to_string(senderId) + ": not an authorized editor.");
    return;
  }

  switch (message.getType())
  {
    case net::MessageType::join:
      handleJoin(message, senderId);
      break;

    case net::MessageType::inputState:
      handleInputState(message, senderId);
      break;

    case net::MessageType::possessSlot:
      handlePossessSlot(message, senderId);
      break;

    default:
      handleEditorMessage(message);
      break;
  }
}

void ServerApp::handleEditorMessage(const net::Message& message) const
{
  switch (message.getType())
  {
    case net::MessageType::editComponent:
      handleEditComponent(message);
      break;

    case net::MessageType::sceneEdit:
      handleSceneEdit(message);
      break;

    case net::MessageType::loadProject:
      handleLoadProject(message);
      break;

    case net::MessageType::addAsset:
      handleAddAsset(message);
      break;

    case net::MessageType::renameAsset:
      handleRenameAsset(message);
      break;

    case net::MessageType::removeAsset:
      handleRemoveAsset(message);
      break;

    case net::MessageType::sceneControl:
      handleSceneControl(message);
      break;

    default: break;
  }
}

bool ServerApp::isMessageAuthorized(const net::Message& message, const int32_t senderId) const
{
  return !net::isMutationMessage(message.getType()) || (m_options.editMode && m_netServer->isEditor(senderId));
}

void ServerApp::handleJoin(const net::Message& message, const int32_t senderId)
{
  // A client joined: bind it to a player slot (so its input routes to that player), tell it whether this
  // server is editable, then send the full project/scene as a Snapshot. Record that a connection has been
  // seen so an ephemeral server (exitWhenEmpty) can later exit when the last one drops.
  m_hasConnected = true;
  assignPlayerSlot(senderId);

  // If the client tagged its join with a nonce (players and the editor do), tell it which slot it got so
  // it can render through that player's camera. Broadcasting with nonce correlation avoids a
  // per-connection send path - every client hears it, only the matching one keeps it.
  net::MessageReader reader(message);
  if (reader.remaining() >= sizeof(uint64_t))
  {
    m_joinNonces[senderId] = reader.read<uint64_t>();
    broadcastPlayerSlot(senderId);
  }

  broadcastEditStatus();
  broadcastSnapshot();
}

int32_t ServerApp::assignPlayerSlot(const int32_t connId)
{
  if (const auto existing = m_playerSlots.slotOf(connId))
  {
    return *existing;
  }

  const int32_t slot = m_playerSlots.assign(connId);
  Log::info(LogCategory::server, "Bound connection " + std::to_string(connId) + " to player slot " + std::to_string(slot) + ".");
  return slot;
}

void ServerApp::broadcastPlayerSlot(const int32_t connId)
{
  const auto nonce = m_joinNonces.find(connId);
  const auto slot = m_playerSlots.slotOf(connId);
  if (nonce == m_joinNonces.end() || !slot)
  {
    return;
  }

  m_netServer->broadcast(replication::buildPlayerSlot(nonce->second, *slot));
}

void ServerApp::handlePossessSlot(const net::Message& message, const int32_t senderId)
{
  // A view/input choice rather than a scene edit, so it is not a mutation message; it is still limited to
  // editor connections, since a player must not be able to move itself into someone else's character.
  if (!m_netServer->isEditor(senderId))
  {
    Log::warn(LogCategory::server, "Discarded a possessSlot from connection " + std::to_string(senderId)
      + ": not an editor.");
    return;
  }

  const auto requested = replication::parsePossessSlot(message);
  if (!requested)
  {
    return;
  }

  const auto outcome = m_playerSlots.request(senderId, *requested);
  const std::string connection = "connection " + std::to_string(senderId);

  if (outcome.result == PossessResult::granted)
  {
    if (outcome.previousSlot)
    {
      InputState::removeSlot(*outcome.previousSlot);
    }

    Log::info(LogCategory::server, "Bound " + connection + " to player slot " + std::to_string(*requested) + ".");
  }
  else if (outcome.result == PossessResult::held)
  {
    Log::warn(LogCategory::server, "Refused " + connection + " player slot " + std::to_string(*requested)
      + ": another connection holds it.");
  }
  else if (outcome.result == PossessResult::outOfRange)
  {
    Log::warn(LogCategory::server, "Refused " + connection + " player slot " + std::to_string(*requested)
      + ": outside 0.." + std::to_string(maxPlayerSlot) + ".");
  }

  broadcastPlayerSlot(senderId);
}

void ServerApp::handleDisconnect(const int32_t connId)
{
  m_joinNonces.erase(connId);

  const auto slot = m_playerSlots.release(connId);
  if (!slot)
  {
    return;
  }

  InputState::removeSlot(*slot);

  Log::info(LogCategory::server, "Connection " + std::to_string(connId) + " dropped; freed player slot "
    + std::to_string(*slot) + ".");
}

void ServerApp::handleInputState(const net::Message& message, const int32_t senderId)
{
  // The client's captured keyboard state, dropped into its player slot so two players don't clobber each
  // other. assignPlayerSlot is idempotent (the slot usually exists from the join), but bind on demand in
  // case input arrives first - done before the parse below so even a malformed message still binds the
  // connection to a slot.
  const int32_t slot = assignPlayerSlot(senderId);

  // The parse itself (including the bound on the key count and the tolerance for a pre-mouse-block
  // client) lives in replication::parseInputState so it can be tested without booting the CLR; this is
  // left with just the slot routing and applying the result.
  const auto payload = replication::parseInputState(message);
  if (!payload.has_value())
  {
    return;
  }

  // Applied only once the message is known to be well formed, so a malformed one leaves the slot exactly
  // as its last good message left it instead of half-updating it.
  InputState::setFocused(slot, payload->focused);
  InputState::setKeysPressed(slot, payload->keysPressed);

  if (payload->hasMouse)
  {
    InputState::setMouse(slot, payload->mouseX, payload->mouseY, payload->mouseDeltaX, payload->mouseDeltaY,
                         payload->scrollY, payload->buttons);
  }
}

void ServerApp::handleSceneControl(const net::Message& message) const
{
  net::MessageReader reader(message);

  net::SceneControlOp op;
  try
  {
    op = reader.read<net::SceneControlOp>();
  }
  catch (const std::exception&)
  {
    return;
  }

  if (op == net::SceneControlOp::loadScene)
  {
    try
    {
      loadScene(reader.readString());
    }
    catch (const std::exception&)
    {
    }
    return;
  }

  const auto scene = m_sceneManager->getCurrentScene();
  if (!scene)
  {
    return;
  }

  const bool wasStopped = m_sceneManager->getSceneStatus() == SceneStatus::stopped;

  try
  {
    applySceneControl(op, *scene->getObjectManager(), wasStopped);
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::server, e.what());
  }

  // Stop resets transforms to their initial values and start/pause change the sim state; re-snapshot so
  // every view reflects it immediately.
  broadcastSnapshot();
}

void ServerApp::applySceneControl(const net::SceneControlOp op, ObjectManager& objectManager, const bool wasStopped) const
{
  if (op == net::SceneControlOp::start)
  {
    m_sceneManager->startScene();

    // Only attach + start the scripts on a real stopped -> running transition (resume from pause
    // keeps the live instances).
    if (wasStopped)
    {
      m_scriptSystem->start(objectManager);

      // Fresh run: drop any contact history from the previous run so its first tick doesn't fire
      // spurious enter/exit events against stale pairs.
      m_collisionSystem->reset();
    }
  }
  else if (op == net::SceneControlOp::pause)
  {
    m_sceneManager->pauseScene();
  }
  else if (op == net::SceneControlOp::stop)
  {
    if (!wasStopped)
    {
      m_scriptSystem->stop(objectManager);
      m_sceneManager->resetScene();
      m_collisionSystem->reset();
    }
  }
}
