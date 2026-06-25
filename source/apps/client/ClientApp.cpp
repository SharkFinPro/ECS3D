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
#include <NetClient.h>
#include <ManagedHost.h>
#include <VulkanEngine/VulkanEngine.h>
#include <nlohmann/json.hpp>

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

  // TODO: if m_options.launchLocalServer, spawn a child ECS3DServer process for m_options.project
  // TODO:   (no --edit) and point the connection at localhost — same netcode path as remote MP.
  m_netClient->connect(m_options.host, m_options.port, net::Role::player, "");

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

    // TODO: capture local input and send it as MessageType::inputState for the server's scripts.

    variableUpdate();
  }
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
      // Full state on join: rebuild the replicated scene from the project blob.
      m_projectSerializer->deserialize(json);
      break;
    case net::MessageType::stateDelta:
      if (const auto scene = m_sceneManager->getCurrentScene())
      {
        replication::applyStateDelta(*scene->getObjectManager(), json);
      }
      break;
    default:
      break;
  }
}
