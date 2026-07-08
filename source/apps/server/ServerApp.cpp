#include "ServerApp.h"
#include "DefaultProject.h"
#include <ComponentRegistry.h>
#include <ComponentRegistration.h>
#include <ProjectSerializer.h>
#include <ProjectPacker.h>
#include <Replication.h>
#include <assets/AssetRegistry.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <objects/Object.h>
#include <objects/components/Component.h>
#include <PhysicsSystem.h>
#include <CollisionSystem.h>
#include <ScriptSystem.h>
#include <bindings/InputState.h>
#include <bindings/BindingContext.h>
#include <NetServer.h>
#include <ManagedHost.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>

ServerApp::ServerApp(LaunchOptions options)
  : m_options(std::move(options)),
    m_host(std::make_shared<ManagedHost>()),
    m_componentRegistry(std::make_shared<ComponentRegistry>()),
    m_assetRegistry(std::make_shared<AssetRegistry>()),
    m_sceneManager(std::make_shared<SceneManager>()),
    m_previousTime(std::chrono::steady_clock::now())
{
  // Boot the CLR from the net transport's runtimeconfig (ECS3DNet is linked by every app). The
  // ScriptBridge assembly is loaded on top of the same runtime by ScriptSystem.
  m_host->init("net/Transport");

  registerDataComponents(*m_componentRegistry);

  m_projectSerializer = std::make_shared<ProjectSerializer>(m_assetRegistry.get(), m_sceneManager.get(), m_componentRegistry);
  m_projectPacker = std::make_shared<ProjectPacker>(m_assetRegistry.get(), m_sceneManager.get(), m_componentRegistry);
  m_collisionSystem = std::make_shared<CollisionSystem>();
  m_scriptSystem = std::make_shared<ScriptSystem>(m_host);
  m_netServer = std::make_shared<net::NetServer>(m_host);

  if (m_options.project.empty())
  {
    // No project file requested: run the built-in sample (scenes 1-3 + falling balls), generated in
    // code. The procedural scenes can't be a static file, so the whole sample is built here rather than
    // shipped as a project on disk.
    m_projectSerializer->deserialize(buildDefaultProject());
  }
  else if (!m_projectSerializer->load(m_options.project) || !m_sceneManager->getCurrentScene())
  {
    logMessage("Error", "No scene loaded from project '" + m_options.project
      + "' - the server will run but simulate nothing. Check the project path and working directory.");
  }

  m_netServer->start(m_options.port, m_options.editMode, m_options.authToken);

  m_sceneManager->startScene();

  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    try
    {
      m_scriptSystem->start(*scene->getObjectManager());
    }
    catch (const std::exception& e)
    {
      logMessage("Error", e.what());
    }

    // The server is headless (no window), so announce that it's up — otherwise a running server looks
    // like it never started.
    logMessage("Info", "Running scene '" + scene->getName() + "' ("
      + std::to_string(scene->getObjectManager()->getAllObjects().size()) + " objects) on port "
      + std::to_string(m_options.port) + ".");
  }
}

ServerApp::~ServerApp()
{
  if (m_netServer)
  {
    m_netServer->stop();
  }

  if (m_host)
  {
    m_host->shutdown();
  }
}

bool ServerApp::isActive() const
{
  // A dedicated server runs until killed.
  if (!m_options.exitWhenEmpty)
  {
    return true;
  }

  // An editor/client-spawned server is ephemeral: once its spawning app has connected (m_hasConnected,
  // set on the first join), it should exit as soon as the last connection drops, so it doesn't linger
  // if the parent ever fails to terminate it. Until that first connection it stays up through the
  // launch -> connect window (when the count is legitimately still 0).
  return !m_hasConnected || m_netServer->connectionCount() > 0;
}

void ServerApp::run()
{
  while (isActive())
  {
    net::Message message;
    while (m_netServer->poll(message))
    {
      // A bad/malicious message (or a script-bridge hiccup while building a snapshot) must not take the
      // whole server down - that would look like "client connected, then nothing".
      try
      {
        handleClientMessage(message);
      }
      catch (const std::exception& e)
      {
        logMessage("Error", std::string("Failed to handle client message: ") + e.what());
      }
    }

    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - m_previousTime).count();
    m_previousTime = now;

    m_timeAccumulator += dt;

    bool ticked = false;
    uint8_t steps = 0;
    while (m_timeAccumulator >= m_fixedUpdateDt && steps < 3)
    {
      fixedUpdate(m_fixedUpdateDt);

      m_timeAccumulator -= m_fixedUpdateDt;
      ++steps;
      ticked = true;
    }

    // Only stream a delta when the sim actually advanced. The server is headless (no vsync), so without
    // this guard the unbounded loop would flood every client with deltas and stall their drain loops.
    if (ticked)
    {
      // Replicate any runtime spawn/destroy the tick's scripts requested before the delta, so a client
      // has the object (or has dropped it) by the time the delta for this tick references it.
      broadcastStructuralChanges();

      broadcastStateDelta();
    }

    // Don't busy-spin a core between ticks.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void ServerApp::fixedUpdate(const float dt) const
{
  const auto scene = m_sceneManager->getCurrentScene();
  if (!scene || m_sceneManager->getSceneStatus() != SceneStatus::running)
  {
    return;
  }

  auto& objectManager = *scene->getObjectManager();

  // The number crunching, in order: scripts read input (variableUpdate) then queue forces, physics
  // integrates, collisions resolve. variableUpdate runs before fixedUpdate so input-driven force is
  // applied the same tick (the server has no render frame to drive it separately).
  try
  {
    m_scriptSystem->variableUpdate(objectManager);
    m_scriptSystem->fixedUpdate(objectManager, dt);
    PhysicsSystem::fixedUpdate(objectManager, dt);
    m_collisionSystem->fixedUpdate(objectManager);
  }
  catch (const std::exception& e)
  {
    logMessage("Error", e.what());
  }
}

namespace {
  // The editor's mutation messages. A non-edit server admits editors read-only, so it must drop these
  // (the editors also disable them in their UI, but the server stays authoritative about it).
  bool isMutation(const net::MessageType type)
  {
    switch (type)
    {
      case net::MessageType::editComponent:
      case net::MessageType::sceneEdit:
      case net::MessageType::sceneControl:
      case net::MessageType::loadProject:
      case net::MessageType::addAsset:
        return true;
      default:
        return false;
    }
  }
}

void ServerApp::handleClientMessage(const net::Message& message)
{
  // A non-edit server is read-only: it serves snapshots/deltas to editors that connect to view it, but
  // never applies their edits.
  if (!m_options.editMode && isMutation(message.getType()))
  {
    return;
  }

  switch (message.getType())
  {
    case net::MessageType::join:
      handleJoin(message);
      break;

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

    case net::MessageType::inputState:
      handleInputState(message);
      break;

    case net::MessageType::sceneControl:
      handleSceneControl(message);
      break;

    default: break;
  }
}

void ServerApp::handleJoin(const net::Message& message)
{
  // A client joined: tell it whether this server is editable, then send the full project/scene as a
  // Snapshot. Record that a connection has been seen so an ephemeral server (exitWhenEmpty) can
  // later exit when the last one drops.
  m_hasConnected = true;
  broadcastEditStatus();
  broadcastSnapshot();
}

void ServerApp::handleEditComponent(const net::Message& message) const
{
  // An editor changed a component: apply it to the authoritative scene, then re-broadcast so every
  // other view (and the editing client, idempotently) converges.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    replication::applyComponentEdit(*scene->getObjectManager(), message);

    m_netServer->broadcast(message);

    // If the edit targets a Script, push the new field values into the live C# instance so the
    // running behavior reflects the change immediately (applyComponentEdit only updates the data
    // layer; the C# instance is owned by ScriptSystem and needs an explicit write). The packed layout
    // mirrors Script::pack: [object uuid][type][className][fields].
    net::MessageReader reader(message);
    const auto objectUUID = uuids::uuid::from_string(reader.readString());

    if (objectUUID.has_value() && reader.read<ComponentType>() == ComponentType::script)
    {
      const auto className = reader.readString();
      const auto fields = nlohmann::json::parse(reader.readString(), nullptr, false);

      if (!fields.is_discarded())
      {
        m_scriptSystem->applyScriptFieldEdit(objectUUID.value(), className, fields);
      }
    }
  }
}

void ServerApp::handleSceneEdit(const net::Message& message) const
{
  // An editor changed the scene graph (add/remove object or component): apply it, then re-snapshot
  // so every view rebuilds (structural changes aren't replicated per-op).
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    const std::string payload(message.bytes().begin(), message.bytes().end());

    const auto json = nlohmann::json::parse(payload, nullptr, false);
    if (!json.is_discarded())
    {
      replication::applySceneEdit(*scene->getObjectManager(), json);

      broadcastSnapshot();
    }
  }
}

void ServerApp::handleLoadProject(const net::Message& message) const
{
  // An editor opened a different project: stop the current scripts, swap the project in, restart,
  // and snapshot so every view rebuilds. The blob is sent (not a path) so it works off-machine too.
  logMessage("Info", "Received loadProject (" + std::to_string(message.bytes().size()) + " bytes).");

  // Stop the current scripts before the scene is swapped out from under them.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    try
    {
      m_scriptSystem->stop(*scene->getObjectManager());
    }
    catch (const std::exception& e)
    {
      logMessage("Error", e.what());
    }
  }

  try
  {
    // Same packed shape as a snapshot: unpack clears + rebuilds the AssetRegistry/SceneManager and loads
    // the current scene. ProjectSerializer stays the JSON path for file save/load.
    m_projectPacker->unpack(message);
  }
  catch (const std::exception& e)
  {
    logMessage("Error", std::string("Failed to load project from editor: ") + e.what());
    return;
  }

  m_sceneManager->startScene();

  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    try
    {
      m_scriptSystem->start(*scene->getObjectManager());
    }
    catch (const std::exception& e)
    {
      logMessage("Error", e.what());
    }

    logMessage("Info", "Loaded project from editor: scene '" + scene->getName() + "' ("
      + std::to_string(scene->getObjectManager()->getAllObjects().size()) + " objects).");
  }

  broadcastSnapshot();
}

void ServerApp::handleAddAsset(const net::Message& message) const
{
  // An editor imported/created an asset: register it in the authoritative registry and re-snapshot.
  nlohmann::json asset;
  try
  {
    asset = replication::unpackAddAsset(message);
  }
  catch (const std::exception&)
  {
    return;
  }

  replication::applyAddAsset(*m_assetRegistry, *m_sceneManager, m_componentRegistry, asset);

  logMessage("Info", "Registered asset (" + asset.value("assetType", std::string{}) + ").");

  broadcastSnapshot();
}

void ServerApp::handleInputState(const net::Message& message)
{
  // The client's captured keyboard state for this frame; the scripts' InputUtils bindings read it.
  net::MessageReader reader(message);
  const auto focused = reader.read<bool>();
  InputState::setFocused(focused);

  const auto numKeys = reader.read<size_t>();
  std::vector<int> keysPressed(numKeys);
  for (auto& key : keysPressed)
  {
    key = reader.read<int>();
  }

  InputState::setKeysPressed(keysPressed);
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
  const auto previousStatus = m_sceneManager->getSceneStatus();

  try
  {
    if (op == net::SceneControlOp::start)
    {
      m_sceneManager->startScene();

      // Only attach + start the scripts on a real stopped -> running transition (resume from pause
      // keeps the live instances).
      if (previousStatus == SceneStatus::stopped)
      {
        m_scriptSystem->start(objectManager);
      }
    }
    else if (op == net::SceneControlOp::pause)
    {
      m_sceneManager->pauseScene();
    }
    else if (op == net::SceneControlOp::stop)
    {
      if (previousStatus != SceneStatus::stopped)
      {
        m_scriptSystem->stop(objectManager);
        m_sceneManager->resetScene();
      }
    }
  }
  catch (const std::exception& e)
  {
    logMessage("Error", e.what());
  }

  // Stop resets transforms to their initial values and start/pause change the sim state; re-snapshot so
  // every view reflects it immediately.
  broadcastSnapshot();
}

void ServerApp::loadScene(const std::string& sceneUUID) const
{
  const auto parsed = uuids::uuid::from_string(sceneUUID);
  if (!parsed.has_value())
  {
    return;
  }

  const auto scene = m_sceneManager->getScene(parsed.value());
  if (!scene)
  {
    return;
  }

  // Stop the outgoing scene's scripts before switching the active scene.
  if (const auto current = m_sceneManager->getCurrentScene())
  {
    try
    {
      m_scriptSystem->stop(*current->getObjectManager());
    }
    catch (const std::exception& e)
    {
      logMessage("Error", e.what());
    }
  }

  m_sceneManager->loadScene(scene);
  m_sceneManager->startScene();

  try
  {
    m_scriptSystem->start(*scene->getObjectManager());
  }
  catch (const std::exception& e)
  {
    logMessage("Error", e.what());
  }

  logMessage("Info", "Switched to scene '" + scene->getName() + "' ("
    + std::to_string(scene->getObjectManager()->getAllObjects().size()) + " objects).");

  broadcastSnapshot();
}

void ServerApp::broadcastSnapshot() const
{
  // Refresh each Script's field blob from its live C# instance so the snapshot carries current values.
  // This reaches into the script bridge, so guard it: a field-sync hiccup must NOT stop the snapshot
  // from going out (that would leave the client with no scene at all).
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    try
    {
      m_scriptSystem->attachAll(*scene->getObjectManager());
      m_scriptSystem->syncFieldsToData(*scene->getObjectManager());
    }
    catch (const std::exception& e)
    {
      logMessage("Error", std::string("syncFieldsToData failed, sending snapshot with last-known field values: ") + e.what());
    }
  }

  // Binary snapshot: ProjectPacker writes the same project state ProjectSerializer::serialize would,
  // but tightly packed instead of JSON. ProjectSerializer stays the JSON path for file save/load.
  net::Message message(net::MessageType::snapshot);
  m_projectPacker->pack(message);

  const auto currentScene = m_sceneManager->getCurrentScene();
  logMessage("Info", "Broadcasting snapshot: " + std::to_string(m_sceneManager->getScenes().size())
    + " scene(s), " + std::to_string(message.size()) + " bytes, currentScene='"
    + (currentScene ? uuids::to_string(currentScene->getUUID()) : std::string{}) + "'.");

  m_netServer->broadcast(message);

  broadcastSceneStatus();
}

void ServerApp::broadcastSceneStatus() const
{
  net::Message message(net::MessageType::sceneStatus);
  message.write(m_sceneManager->getSceneStatus());

  m_netServer->broadcast(message);
}

void ServerApp::broadcastEditStatus() const
{
  net::Message message(net::MessageType::editStatus);
  message.write(m_options.editMode);

  m_netServer->broadcast(message);
}

void ServerApp::broadcastStateDelta() const
{
  const auto scene = m_sceneManager->getCurrentScene();
  if (!scene)
  {
    return;
  }

  // Binary state delta: packStateDelta writes each object's uuid + local transform straight into the
  // message, instead of the heavier JSON dump this used to broadcast every tick.
  net::Message message(net::MessageType::stateDelta);
  replication::packStateDelta(message, *scene->getObjectManager());
  m_netServer->broadcast(message);
}

void ServerApp::broadcastStructuralChanges() const
{
  // The spawn/destroy bindings buffered what the scripts did on BindingContext (scripting can't reach the
  // net layer). Broadcast spawns before destroys, then remove the marked objects from the authoritative
  // scene. A spawned object is still live here, so its packed blob carries current transform/components.
  const auto spawned = BindingContext::takeSpawned();
  for (const auto& object : spawned)
  {
    m_netServer->broadcast(replication::buildObjectSpawned(*object));
  }

  const auto destroyed = BindingContext::takeDestroyed();
  for (const auto& uuid : destroyed)
  {
    m_netServer->broadcast(replication::buildObjectDestroyed(uuid));
  }

  if (!destroyed.empty())
  {
    if (const auto scene = m_sceneManager->getCurrentScene())
    {
      scene->getObjectManager()->deleteObjectsMarkedForDeletion();
    }
  }
}

void ServerApp::logMessage(const std::string& level, const std::string& message)
{
  std::cerr << "[" << level << "] " << message << std::endl;
}
