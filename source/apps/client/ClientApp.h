#ifndef CLIENTAPP_H
#define CLIENTAPP_H

#include <memory>
#include <string>

namespace vke {
  class VulkanEngine;
}

class ManagedHost;
class ComponentRegistry;
class AssetRegistry;
class SceneManager;
class ProjectSerializer;
class GpuAssetCache;
class RenderSystem;

namespace net {
  class NetClient;
  struct Message;
}

// The lightweight view. It renders + sends input; it never links ECS3DSim/ECS3DScripting. It holds a
// replicated scene it applies Snapshot/StateDelta into.
class ClientApp {
public:
  struct ConnectOptions {
    std::string host = "127.0.0.1";
    int port = 0;
    bool launchLocalServer = false;
    std::string project;
  };

  explicit ClientApp(ConnectOptions options);

  ~ClientApp();

  [[nodiscard]] bool isActive() const;

  void run();

private:
  ConnectOptions m_options;

  std::shared_ptr<ManagedHost> m_host;
  std::shared_ptr<net::NetClient> m_netClient;

  std::shared_ptr<ComponentRegistry> m_componentRegistry;
  std::shared_ptr<AssetRegistry> m_assetRegistry;
  std::shared_ptr<SceneManager> m_sceneManager;
  std::shared_ptr<ProjectSerializer> m_projectSerializer;

  std::shared_ptr<vke::VulkanEngine> m_renderer;
  std::shared_ptr<GpuAssetCache> m_assetCache;
  std::shared_ptr<RenderSystem> m_renderSystem;

  void createRenderer();

  void variableUpdate();

  void applyMessage(const net::Message& message);
};



#endif //CLIENTAPP_H
