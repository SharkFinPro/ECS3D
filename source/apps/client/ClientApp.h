#ifndef CLIENTAPP_H
#define CLIENTAPP_H

#include <memory>
#include <string>

namespace vke {
  class VulkanEngine;
}

class ManagedHost;
class RenderSystem;
class GpuAssetCache;

namespace net {
  class NetClient;
}

class ClientApp {
public:
  struct ConnectOptions {
    std::string host = "127.0.0.1";
    int port = 0;
    bool launchLocalServer = false;
    std::string project;
  };

  explicit ClientApp(ConnectOptions options);

  [[nodiscard]] bool isActive() const;

  void run();

private:
  ConnectOptions m_options;

  std::shared_ptr<ManagedHost> m_host;
  std::shared_ptr<net::NetClient> m_netClient;

  std::shared_ptr<vke::VulkanEngine> m_renderer;
  std::shared_ptr<RenderSystem> m_renderSystem;
  std::shared_ptr<GpuAssetCache> m_assetCache;

  void variableUpdate();

  // TODO: launchLocalServer spawns a child ECS3DServer (no --edit) and connects via localhost,
  // TODO:   so singleplayer uses the exact same netcode path as remote multiplayer.
};



#endif //CLIENTAPP_H
