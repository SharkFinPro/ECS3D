#ifndef EDITORAPP_H
#define EDITORAPP_H

#include <memory>
#include <string>

namespace vke {
  class VulkanEngine;
}

class ManagedHost;
class RenderSystem;
class GpuAssetCache;
class ComponentEditor;
class AssetBrowserPanel;

namespace net {
  class NetClient;
}

class EditorApp {
public:
  struct LaunchOptions {
    std::string project;
    int port = 0;
  };

  explicit EditorApp(LaunchOptions options);

  [[nodiscard]] bool isActive() const;

  void run();

private:
  LaunchOptions m_options;

  std::shared_ptr<ManagedHost> m_host;
  std::shared_ptr<net::NetClient> m_netClient;

  std::shared_ptr<vke::VulkanEngine> m_renderer;
  std::shared_ptr<RenderSystem> m_renderSystem;
  std::shared_ptr<GpuAssetCache> m_assetCache;

  std::shared_ptr<ComponentEditor> m_componentEditor;
  std::shared_ptr<AssetBrowserPanel> m_assetBrowser;

  void updateGui();

  void variableUpdate();

  // TODO: the editor spawns a child ECS3DServer with --edit and an auth token, then connects via
  // TODO:   localhost as Role::editor. Edits become commands sent to the server, which owns the scene.
};



#endif //EDITORAPP_H
