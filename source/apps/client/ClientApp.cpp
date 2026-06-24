#include "ClientApp.h"

ClientApp::ClientApp(ConnectOptions options)
  : m_options(std::move(options))
{
  // TODO: build the lightweight view:
  // TODO:   - ManagedHost: boot the CoreCLR runtime (for the net transport only; no scripting).
  // TODO:   - if m_options.launchLocalServer: spawn a child ECS3DServer process for m_options.project.
  // TODO:   - NetClient: connect(m_options.host, m_options.port, Role::player, "").
  // TODO:   - VulkanEngine renderer + RenderSystem + GpuAssetCache.
}

bool ClientApp::isActive() const
{
  // TODO: active while the render window is open and the connection is alive.
  return false;
}

void ClientApp::run()
{
  // TODO: per frame:
  // TODO:   1. drain NetClient inbox and apply Snapshot/StateDelta to the replicated scene view.
  // TODO:   2. capture local input and send it as net::InputState to the server.
  // TODO:   3. variableUpdate() to render the view.
  while (isActive())
  {
    variableUpdate();
  }
}

void ClientApp::variableUpdate()
{
  // TODO: run the RenderSystem over the replicated scene, then renderer->render().
}
