#include "EditorApp.h"

EditorApp::EditorApp(LaunchOptions options)
  : m_options(std::move(options))
{
  // TODO: build the editor view + tooling:
  // TODO:   - ManagedHost: boot the CoreCLR runtime (net transport only; the server runs scripts).
  // TODO:   - spawn a child ECS3DServer with --edit and a generated auth token for m_options.project.
  // TODO:   - NetClient: connect("127.0.0.1", m_options.port, Role::editor, authToken).
  // TODO:   - VulkanEngine renderer + RenderSystem + GpuAssetCache (same as the client).
  // TODO:   - ComponentEditor + AssetBrowserPanel: register the per-type editing widgets via the
  // TODO:     per-component register functions (registerTransformEditor, registerRigidBodyEditor, ...).
}

bool EditorApp::isActive() const
{
  // TODO: active while the render window is open and the (local) server connection is alive.
  return false;
}

void EditorApp::run()
{
  // TODO: per frame: drain NetClient inbox -> apply to the replicated scene; updateGui() for the
  // TODO:   editor panels (which emit edit commands back to the server); variableUpdate() to render.
  while (isActive())
  {
    updateGui();

    variableUpdate();
  }
}

void EditorApp::updateGui()
{
  // TODO: draw the menu bar, dock space, ObjectGUIManager, AssetBrowserPanel, and the selected
  // TODO:   object's ComponentEditor widgets. Mutations are sent to the server, not applied locally.
}

void EditorApp::variableUpdate()
{
  // TODO: run the RenderSystem over the replicated scene, then renderer->render().
}
