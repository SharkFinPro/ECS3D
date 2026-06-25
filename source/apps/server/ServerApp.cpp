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
#include <NetServer.h>
#include <ManagedHost.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>

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

  m_projectSerializer->load(m_options.project);

  m_netServer->start(m_options.port, m_options.editMode);

  // The server is authoritative, so it runs the scene immediately (the old SceneManager started on a
  // user "Start"; here the server simulates as soon as it is up).
  m_sceneManager->startScene();
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
      handleClientMessage(message);
    }

    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - m_previousTime).count();
    m_previousTime = now;

    m_timeAccumulator += dt;

    uint8_t steps = 1;
    while (m_timeAccumulator >= m_fixedUpdateDt && steps <= 3)
    {
      ++steps;

      fixedUpdate(m_fixedUpdateDt);

      m_timeAccumulator -= m_fixedUpdateDt;
    }

    broadcastStateDelta();
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

  // The number crunching, in order: scripts can apply forces, physics integrates, collisions resolve.
  try
  {
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
    {
      // A client joined: send it the full project/scene as a Snapshot.
      const auto payload = m_projectSerializer->serialize().dump();

      const net::Message snapshot {
        .type = net::MessageType::snapshot,
        .payload = std::vector<uint8_t>(payload.begin(), payload.end())
      };

      m_netServer->broadcast(snapshot);
      break;
    }
    case net::MessageType::inputState:
      // TODO: decode the input and store it in the networked InputState the scripts read (replacing
      // TODO:   the old GLFW-window InputUtils on the headless server).
      break;
    default:
      break;
  }
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
