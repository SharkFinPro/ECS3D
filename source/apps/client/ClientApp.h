#ifndef CLIENTAPP_H
#define CLIENTAPP_H

#include <Protocol.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <uuid.h>

namespace vke {
  class VulkanEngine;
}

class ManagedHost;
class ComponentRegistry;
class AssetRegistry;
class SceneManager;
class ProjectPacker;
class GpuAssetCache;
class RenderSystem;

namespace net {
  class NetClient;
  class ServerProcess;
  class Message;
}

// The lightweight view. It renders + sends input; it never links ECS3DSim/ECS3DScripting. It holds a
// replicated scene it applies Snapshot/StateDelta into.
class ClientApp {
public:
  struct ConnectOptions {
    std::string host = "127.0.0.1";
    int port = net::defaultPort;
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
  std::unique_ptr<net::ServerProcess> m_serverProcess;
  std::shared_ptr<net::NetClient> m_netClient;

  std::shared_ptr<ComponentRegistry> m_componentRegistry;
  std::shared_ptr<AssetRegistry> m_assetRegistry;
  std::shared_ptr<SceneManager> m_sceneManager;
  std::shared_ptr<ProjectPacker> m_projectPacker;

  std::shared_ptr<vke::VulkanEngine> m_renderer;
  std::shared_ptr<GpuAssetCache> m_assetCache;
  std::shared_ptr<RenderSystem> m_renderSystem;

  // Only resend input when it changes, so a second view (e.g. the editor) that isn't focused doesn't
  // clobber this one's keys on the shared server-side InputState every frame.
  std::vector<int> m_lastInputKeys;
  bool m_lastInputFocused = false;
  uint8_t m_lastButtons = 0;
  float m_lastMouseX = 0.0f;
  float m_lastMouseY = 0.0f;
  bool m_inputSent = false;

  // A per-session random tag sent in join; the server echoes it back in a playerSlot message so this
  // client can pick out its own slot assignment from the broadcast (Phase 4.4).
  uint64_t m_joinNonce = 0;
  // The player slot the server bound this client to (-1 until the playerSlot message arrives). Drives
  // which object's Camera the client renders through. mutable: set from the const message-apply path.
  mutable int32_t m_playerSlot = -1;

  void createRenderer();

  void connectToServer();

  void sendInput();

  void variableUpdate() const;

  void applyMessage(const net::Message& message) const;

  void handleSnapshot(const net::Message& message) const;

  void handleStateDelta(const net::Message& message) const;

  void handleEditComponent(const net::Message& message) const;

  void handleObjectSpawned(const net::Message& message) const;

  void handleObjectDestroyed(const net::Message& message) const;

  void handlePlayerSlot(const net::Message& message) const;

  // The object this client should render through: the one carrying a PlayerController for this client's
  // player slot and a Camera. nullopt when the slot is still unknown or no such camera exists — the
  // caller then falls back to the scene's first active camera / free-fly (RenderSystem::updateCamera).
  [[nodiscard]] std::optional<uuids::uuid> resolvePlayerCamera() const;
};



#endif //CLIENTAPP_H
