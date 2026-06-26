#include "ClientApp.h"
#include <ComponentRegistry.h>
#include <ComponentRegistration.h>
#include <ProjectSerializer.h>
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
#include <nlohmann/json.hpp>
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

  m_projectSerializer = std::make_shared<ProjectSerializer>(m_assetRegistry.get(), m_sceneManager.get(), m_componentRegistry);

  createRenderer();

  m_assetCache = std::make_shared<GpuAssetCache>(m_renderer, m_assetRegistry.get());
  m_renderSystem = std::make_shared<RenderSystem>();

  m_netClient = std::make_shared<net::NetClient>(m_host);

  connectToServer();

  // Ask the server for the initial Snapshot.
  m_netClient->send(net::Message{ .type = net::MessageType::join, .payload = {} });
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

  if (m_inputSent && snapshot.keys == m_lastInputKeys && snapshot.focused == m_lastInputFocused)
  {
    return;
  }

  m_lastInputKeys = snapshot.keys;
  m_lastInputFocused = snapshot.focused;
  m_inputSent = true;

  const nlohmann::json payload = {
    { "keys", snapshot.keys },
    { "focused", snapshot.focused }
  };

  const auto dumped = payload.dump();

  m_netClient->send(net::Message{
    .type = net::MessageType::inputState,
    .payload = std::vector<uint8_t>(dumped.begin(), dumped.end())
  });
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
      .maxTextures = 100
    }
  };

  m_renderer = std::make_shared<vke::VulkanEngine>(engineConfig);
}

void ClientApp::variableUpdate()
{
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    m_renderSystem->variableUpdate(*scene->getObjectManager(), *m_assetCache);
  }

  m_renderer->render();
}

void ClientApp::applyMessage(const net::Message& message)
{
  const std::string payload(message.payload.begin(), message.payload.end());

  const auto json = nlohmann::json::parse(payload, nullptr, false);
  if (json.is_discarded())
  {
    return;
  }

  switch (message.type)
  {
    case net::MessageType::snapshot:
    {
      // Full state on join: rebuild the replicated scene from the project blob.
      m_projectSerializer->deserialize(json);

      const auto scene = m_sceneManager->getCurrentScene();
      std::cerr << "[Client] Applied snapshot (" << message.payload.size() << " bytes). Current scene: "
                << (scene ? scene->getName() : "<none>") << " ("
                << (scene ? scene->getObjectManager()->getAllObjects().size() : 0) << " objects)." << std::endl;
      break;
    }
    case net::MessageType::stateDelta:
      if (const auto scene = m_sceneManager->getCurrentScene())
      {
        replication::applyStateDelta(*scene->getObjectManager(), json);
      }
      break;
    case net::MessageType::editComponent:
      // The server applied an editor's component change; mirror it into the replicated scene.
      if (const auto scene = m_sceneManager->getCurrentScene())
      {
        replication::applyComponentEdit(*scene->getObjectManager(), json);
      }
      break;
    default:
      break;
  }
}
