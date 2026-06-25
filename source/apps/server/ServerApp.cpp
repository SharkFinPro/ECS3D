#include "ServerApp.h"
#include <ComponentRegistry.h>
#include <ComponentRegistration.h>
#include <ProjectSerializer.h>
#include <Replication.h>
#include <assets/AssetRegistry.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <PhysicsSystem.h>
#include <CollisionSystem.h>
#include <ScriptSystem.h>
#include <bindings/InputState.h>
#include <NetServer.h>
#include <ManagedHost.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
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
  m_physicsSystem = std::make_shared<PhysicsSystem>();
  m_collisionSystem = std::make_shared<CollisionSystem>();
  m_scriptSystem = std::make_shared<ScriptSystem>(m_host);
  m_netServer = std::make_shared<net::NetServer>(m_host);

  if (!m_projectSerializer->load(m_options.project) || !m_sceneManager->getCurrentScene())
  {
    logMessage("Error", "No scene loaded from project '" + m_options.project
      + "' - the server will run but simulate nothing. Check the project path and working directory.");
  }

  m_netServer->start(m_options.port, m_options.editMode);

  // The server is authoritative, so it runs the scene immediately (the old SceneManager started on a
  // user "Start"; here the server simulates as soon as it is up).
  m_sceneManager->startScene();

  // Attach + start the managed script instances for the running scene (the old Object::start ->
  // Script::start path; the data Script no longer reaches the CLR, so the server drives it here).
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
  // TODO: a dedicated server runs until killed; an editor/client-spawned server should exit when its
  // TODO:   last connection drops. There is no exit condition yet, so this is an open-ended loop.
  return true;
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
      broadcastStateDelta();
    }

    // Don't busy-spin a core between ticks.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void ServerApp::fixedUpdate(const float dt)
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
    m_physicsSystem->fixedUpdate(objectManager, dt, *this);
    m_collisionSystem->fixedUpdate(objectManager, *m_physicsSystem);
  }
  catch (const std::exception& e)
  {
    logMessage("Error", e.what());
  }
}

void ServerApp::handleClientMessage(const net::Message& message)
{
  switch (message.type)
  {
    case net::MessageType::join:
      // A client joined: send it the full project/scene as a Snapshot.
      broadcastSnapshot();
      break;
    case net::MessageType::editComponent:
    {
      // An editor changed a component: apply it to the authoritative scene, then re-broadcast so every
      // other view (and the editing client, idempotently) converges.
      if (const auto scene = m_sceneManager->getCurrentScene())
      {
        const std::string payload(message.payload.begin(), message.payload.end());

        const auto json = nlohmann::json::parse(payload, nullptr, false);
        if (!json.is_discarded())
        {
          replication::applyComponentEdit(*scene->getObjectManager(), json);

          m_netServer->broadcast(message);
        }
      }
      break;
    }
    case net::MessageType::sceneEdit:
    {
      // An editor changed the scene graph (add/remove object or component): apply it, then re-snapshot
      // so every view rebuilds (structural changes aren't replicated per-op).
      if (const auto scene = m_sceneManager->getCurrentScene())
      {
        const std::string payload(message.payload.begin(), message.payload.end());

        const auto json = nlohmann::json::parse(payload, nullptr, false);
        if (!json.is_discarded())
        {
          replication::applySceneEdit(*scene->getObjectManager(), json);

          broadcastSnapshot();
        }
      }
      break;
    }
    case net::MessageType::loadProject:
    {
      // An editor opened a different project: stop the current scripts, swap the project in, restart,
      // and snapshot so every view rebuilds. The blob is sent (not a path) so it works off-machine too.
      const std::string payload(message.payload.begin(), message.payload.end());

      const auto json = nlohmann::json::parse(payload, nullptr, false);
      if (!json.is_discarded())
      {
        loadProject(json);
      }
      break;
    }
    case net::MessageType::inputState:
    {
      // The client's captured keyboard state for this frame; the scripts' InputUtils bindings read it.
      const std::string payload(message.payload.begin(), message.payload.end());

      const auto json = nlohmann::json::parse(payload, nullptr, false);
      if (!json.is_discarded())
      {
        InputState::setKeysPressed(json.value("keys", std::vector<int>{}));
        InputState::setFocused(json.value("focused", false));
      }
      break;
    }
    case net::MessageType::sceneControl:
    {
      const std::string payload(message.payload.begin(), message.payload.end());

      const auto json = nlohmann::json::parse(payload, nullptr, false);
      if (!json.is_discarded())
      {
        applySceneControl(json.value("op", std::string{}));
      }
      break;
    }
    default:
      break;
  }
}

void ServerApp::applySceneControl(const std::string& op)
{
  const auto scene = m_sceneManager->getCurrentScene();
  if (!scene)
  {
    return;
  }

  auto& objectManager = *scene->getObjectManager();
  const auto previousStatus = m_sceneManager->getSceneStatus();

  try
  {
    if (op == "start")
    {
      m_sceneManager->startScene();

      // Only attach + start the scripts on a real stopped -> running transition (resume from pause
      // keeps the live instances).
      if (previousStatus == SceneStatus::stopped)
      {
        m_scriptSystem->start(objectManager);
      }
    }
    else if (op == "pause")
    {
      m_sceneManager->pauseScene();
    }
    else if (op == "stop")
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

void ServerApp::loadProject(const nlohmann::json& project)
{
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
    // deserialize clears + rebuilds the AssetRegistry/SceneManager and loads the current scene.
    m_projectSerializer->deserialize(project);
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

void ServerApp::broadcastSnapshot()
{
  // Refresh each Script's field blob from its live C# instance so the snapshot carries current values.
  // This reaches into the script bridge, so guard it: a field-sync hiccup must NOT stop the snapshot
  // from going out (that would leave the client with no scene at all).
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    try
    {
      m_scriptSystem->syncFieldsToData(*scene->getObjectManager());
    }
    catch (const std::exception& e)
    {
      logMessage("Error", std::string("syncFieldsToData failed, sending snapshot with last-known field values: ") + e.what());
    }
  }

  const auto project = m_projectSerializer->serialize();
  const auto payload = project.dump();

  const auto sceneCount = project.contains("assets") && project.at("assets").contains("scenes")
    ? project.at("assets").at("scenes").size() : 0;
  logMessage("Info", "Broadcasting snapshot: " + std::to_string(sceneCount) + " scene(s), "
    + std::to_string(payload.size()) + " bytes, currentScene='"
    + project.value("currentSceneUUID", std::string{}) + "'.");

  const net::Message snapshot {
    .type = net::MessageType::snapshot,
    .payload = std::vector<uint8_t>(payload.begin(), payload.end())
  };

  m_netServer->broadcast(snapshot);
}

void ServerApp::broadcastStateDelta()
{
  const auto scene = m_sceneManager->getCurrentScene();
  if (!scene)
  {
    return;
  }

  const auto payload = replication::buildStateDelta(*scene->getObjectManager()).dump();

  const net::Message message {
    .type = net::MessageType::stateDelta,
    .payload = std::vector<uint8_t>(payload.begin(), payload.end())
  };

  m_netServer->broadcast(message);
}

void ServerApp::logMessage(const std::string& level, const std::string& message)
{
  std::cerr << "[" << level << "] " << message << std::endl;
}
