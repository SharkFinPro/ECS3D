#include "ServerApp.h"
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <CollisionSystem.h>
#include <ScriptSystem.h>
#include <bindings/InputState.h>
#include <NetServer.h>
#include <Log.h>
#include <nlohmann/json.hpp>

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
  const int32_t slot = assignPlayerSlot(senderId);

  // If the client tagged its join with a nonce (players do; the editor sends none), tell it which slot it
  // got so it can render through that player's camera. Broadcasting with nonce correlation avoids a
  // per-connection send path - every client hears it, only the matching one keeps it.
  net::MessageReader reader(message);
  if (reader.remaining() >= sizeof(uint64_t))
  {
    const auto nonce = reader.read<uint64_t>();
    net::Message reply(net::MessageType::playerSlot);
    reply.write(nonce);
    reply.write(slot);
    m_netServer->broadcast(reply);
  }

  broadcastEditStatus();
  broadcastSnapshot();
}

int32_t ServerApp::assignPlayerSlot(const int32_t connId)
{
  if (const auto it = m_connectionSlots.find(connId); it != m_connectionSlots.end())
  {
    return it->second;
  }

  // Lowest free slot: scan upward until a slot no connection currently holds is found.
  int32_t slot = 0;
  const auto slotTaken = [this](const int32_t candidate) {
    for (const auto& [conn, taken] : m_connectionSlots)
    {
      if (taken == candidate)
      {
        return true;
      }
    }
    return false;
  };
  while (slotTaken(slot))
  {
    ++slot;
  }

  m_connectionSlots.emplace(connId, slot);
  Log::info(LogCategory::server, "Bound connection " + std::to_string(connId) + " to player slot " + std::to_string(slot) + ".");
  return slot;
}

void ServerApp::handleDisconnect(const int32_t connId)
{
  const auto it = m_connectionSlots.find(connId);
  if (it == m_connectionSlots.end())
  {
    return;
  }

  const int32_t slot = it->second;
  m_connectionSlots.erase(it);
  InputState::removeSlot(slot);

  Log::info(LogCategory::server, "Connection " + std::to_string(connId) + " dropped; freed player slot "
    + std::to_string(slot) + ".");
}

void ServerApp::handleInputState(const net::Message& message, const int32_t senderId)
{
  // The client's captured keyboard state, dropped into its player slot so two players don't clobber each
  // other. assignPlayerSlot is idempotent (the slot usually exists from the join), but bind on demand in
  // case input arrives first.
  const int32_t slot = assignPlayerSlot(senderId);

  net::MessageReader reader(message);
  const auto focused = reader.read<bool>();

  // The count arrives from the network, so bound it against what is left of the payload before sizing
  // anything: the message cannot hold more key codes than it has bytes for, and without the check a
  // client asking for a billion keys gets the allocation attempted first and the underflow only after.
  // It is a ceiling, not an exact length - the trailing mouse block is counted as if it could be key
  // codes - so a client that predates that block still degrades to "no mouse" rather than being refused.
  const auto numKeys = reader.read<uint32_t>();
  if (numKeys > reader.remaining() / sizeof(int32_t))
  {
    // Dropped rather than thrown, like every other malformed message here: the drain loop logs what it
    // catches to a flushed stderr, which a client could otherwise spam from the tick thread.
    return;
  }

  std::vector<int> keysPressed(numKeys);
  for (auto& key : keysPressed)
  {
    key = reader.read<int32_t>();
  }

  // Applied only once the message is known to be well formed, so a malformed one leaves the slot exactly
  // as its last good message left it instead of half-updating it.
  InputState::setFocused(slot, focused);
  InputState::setKeysPressed(slot, keysPressed);

  // Mouse block, appended after the keys (see Protocol.h). Guard on remaining() so an older client that
  // predates mouse input degrades to "no mouse" instead of throwing an underflow.
  constexpr size_t mouseBytes = 5 * sizeof(float) + sizeof(uint8_t);
  if (reader.remaining() >= mouseBytes)
  {
    const auto mouseX = reader.read<float>();
    const auto mouseY = reader.read<float>();
    const auto mouseDeltaX = reader.read<float>();
    const auto mouseDeltaY = reader.read<float>();
    const auto scrollY = reader.read<float>();
    const auto buttons = reader.read<uint8_t>();
    InputState::setMouse(slot, mouseX, mouseY, mouseDeltaX, mouseDeltaY, scrollY, buttons);
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

  auto& objectManager = *scene->getObjectManager();
  const bool wasStopped = m_sceneManager->getSceneStatus() == SceneStatus::stopped;

  try
  {
    applySceneControl(op, objectManager, wasStopped);
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
