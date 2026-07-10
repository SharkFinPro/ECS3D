#include "ClientApp.h"
#include <ComponentRegistry.h>
#include <ComponentRegistration.h>
#include <ProjectPacker.h>
#include <Replication.h>
#include <assets/AssetRegistry.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <GpuAssetCache.h>
#include <RenderSystem.h>
#include <InputCapture.h>
#include <NetClient.h>
#include <ServerProcess.h>
#include <ManagedHost.h>
#include <VulkanEngine/VulkanEngine.h>
#include <chrono>
#include <iostream>
#include <thread>

ClientApp::ClientApp(ConnectOptions options)
  : m_options(std::move(options)),
    m_host(std::make_shared<ManagedHost>()),
    m_componentRegistry(std::make_shared<ComponentRegistry>()),
    m_assetRegistry(std::make_shared<AssetRegistry>()),
    m_sceneManager(std::make_shared<SceneManager>())
{
  // Boot the CLR from the net transport's runtimeconfig (the client only needs the socket assembly).
  m_host->init("net/Transport");

  registerDataComponents(*m_componentRegistry);

  m_projectPacker = std::make_shared<ProjectPacker>(m_assetRegistry.get(), m_sceneManager.get(), m_componentRegistry);

  createRenderer();

  m_assetCache = std::make_shared<GpuAssetCache>(m_renderer, m_assetRegistry.get());
  m_renderSystem = std::make_shared<RenderSystem>();

  m_netClient = std::make_shared<net::NetClient>(m_host);

  connectToServer();

  // Ask the server for the initial Snapshot.
  const net::Message message(net::MessageType::join);
  m_netClient->send(message);
}

ClientApp::~ClientApp()
{
  if (m_netClient)
  {
    m_netClient->disconnect();
  }

  if (m_host)
  {
    m_host->shutdown();
  }
}

bool ClientApp::isActive() const
{
  return m_renderer->isActive();
}

void ClientApp::run()
{
  while (isActive())
  {
    net::Message message;
    while (m_netClient->poll(message))
    {
      applyMessage(message);
    }

    sendInput();

    variableUpdate();
  }
}

void ClientApp::sendInput()
{
  const auto snapshot = input::capture(*m_renderer);

  // Discrete state we de-dup on (don't resend an unchanged keyboard/button/cursor every frame). Cursor
  // position is included so mouse movement triggers a send; delta rides along in the same message.
  const bool discreteChanged = !m_inputSent
    || snapshot.keys != m_lastInputKeys
    || snapshot.focused != m_lastInputFocused
    || snapshot.buttons != m_lastButtons
    || snapshot.mouseX != m_lastMouseX
    || snapshot.mouseY != m_lastMouseY;

  // Scroll is a per-frame amount with no resting position, so it must be sent on any frame it's non-zero
  // (it isn't captured by the position comparison above).
  const bool scrolled = snapshot.scrollY != 0.0f;

  if (m_inputSent && !discreteChanged && !scrolled)
  {
    return;
  }

  m_lastInputKeys = snapshot.keys;
  m_lastInputFocused = snapshot.focused;
  m_lastButtons = snapshot.buttons;
  m_lastMouseX = snapshot.mouseX;
  m_lastMouseY = snapshot.mouseY;
  m_inputSent = true;

  net::Message message(net::MessageType::inputState);
  message.write(snapshot.focused);
  message.write(snapshot.keys.size());
  for (const auto& key : snapshot.keys)
  {
    message.write(key);
  }

  message.write(snapshot.mouseX);
  message.write(snapshot.mouseY);
  message.write(snapshot.mouseDeltaX);
  message.write(snapshot.mouseDeltaY);
  message.write(snapshot.scrollY);
  message.write(snapshot.buttons);

  m_netClient->send(message);
}

void ClientApp::connectToServer()
{
  using namespace std::chrono_literals;

  // Singleplayer: spawn a local server (same netcode path as remote, just over loopback).
  if (m_options.launchLocalServer)
  {
    m_serverProcess = std::make_unique<net::ServerProcess>();
    // --ephemeral: this spawned server should exit when its last connection drops, so it can't outlive
    // the client if the RAII terminate is ever missed (e.g. an abnormal exit).
    if (!m_serverProcess->launch("ECS3DServer", "--ephemeral"))
    {
      std::cerr << "[Client] Failed to launch local server (ECS3DServer) next to this executable." << std::endl;
    }
  }

  // The server (especially a just-spawned one) needs a moment to boot the CLR and start listening, so
  // retry the connection rather than failing on the first refused attempt.
  const auto deadline = std::chrono::steady_clock::now() + 15s;
  do
  {
    m_netClient->connect(m_options.host, m_options.port, net::Role::player, "");

    if (m_netClient->isConnected())
    {
      return;
    }

    std::this_thread::sleep_for(250ms);
  }
  while (std::chrono::steady_clock::now() < deadline);

  std::cerr << "[Client] Could not connect to " << m_options.host << ":" << m_options.port << "." << std::endl;
}

void ClientApp::createRenderer()
{
  // Ported from ECS3D::initRenderer. The client is a lightweight view, so no custom ImGui style.
  const vke::EngineConfig engineConfig {
    .window {
      .width = 1280,
      .height = 720,
      .title = "ECS3D Client"
    },
    .camera {
      .position = { 0, 5, -50 }
    },
    .imGui {
      .useDockspace = false,
      .maxTextures = 100
    }
  };

  m_renderer = std::make_shared<vke::VulkanEngine>(engineConfig);
}

void ClientApp::variableUpdate() const
{
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    m_renderSystem->variableUpdate(*scene->getObjectManager(), *m_assetCache);

    // Render through the scene's active component Camera when one exists (Phase 4.2); falls back to the
    // built-in free-fly camera otherwise. Phase 4.4 narrows this to the client's own player camera.
    m_renderSystem->updateCamera(*scene->getObjectManager(), *m_assetCache);
  }

  m_renderer->render();
}

void ClientApp::applyMessage(const net::Message& message) const
{
  switch (message.getType())
  {
    case net::MessageType::snapshot:
      handleSnapshot(message);
      break;

    case net::MessageType::stateDelta:
      handleStateDelta(message);
      break;

    case net::MessageType::editComponent:
      handleEditComponent(message);
      break;

    case net::MessageType::objectSpawned:
      handleObjectSpawned(message);
      break;

    case net::MessageType::objectDestroyed:
      handleObjectDestroyed(message);
      break;

    default: break;
  }
}

void ClientApp::handleSnapshot(const net::Message& message) const
{
  const auto scene = m_sceneManager->getCurrentScene();

  // Full state on join: rebuild the replicated scene from the packed project blob.
  m_projectPacker->unpack(message);
  std::cerr << "[Client] Applied snapshot (" << message.size() << " bytes). Current scene: "
            << (scene ? scene->getName() : "<none>") << " ("
            << (scene ? scene->getObjectManager()->getAllObjects().size() : 0) << " objects)." << std::endl;
}

void ClientApp::handleStateDelta(const net::Message& message) const
{
  const auto scene = m_sceneManager->getCurrentScene();

  if (scene)
  {
    replication::unpackStateDelta(*scene->getObjectManager(), message);
  }
}

void ClientApp::handleEditComponent(const net::Message& message) const
{
  // The server applied an editor's component change; mirror it into the replicated scene.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    replication::applyComponentEdit(*scene->getObjectManager(), message);
  }
}

void ClientApp::handleObjectSpawned(const net::Message& message) const
{
  // A script spawned an object at runtime; splice the packed object into the replicated scene.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    replication::applyObjectSpawned(*scene->getObjectManager(), message);
  }
}

void ClientApp::handleObjectDestroyed(const net::Message& message) const
{
  // A script destroyed an object at runtime; drop it from the replicated scene.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    replication::applyObjectDestroyed(*scene->getObjectManager(), message);
  }
}
