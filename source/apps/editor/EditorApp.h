#ifndef EDITORAPP_H
#define EDITORAPP_H

#include <VulkanEngine/components/window/Window.h>
#include <Protocol.h>
#include <memory>
#include <string>
#include <vector>

namespace vke {
  class VulkanEngine;
}

class ManagedHost;
class ComponentRegistry;
class AssetRegistry;
class SceneManager;
class ProjectSerializer;
class RenderSystem;
class GpuAssetCache;
class ComponentEditor;
class ObjectGUIManager;
class AssetBrowserPanel;
class SaveUI;

namespace net {
  class NetClient;
  class ServerProcess;
  struct Message;
}

// The editor is a client + tooling: it renders the replicated scene (Snapshot/StateDelta, same as the
// client) and adds the ImGui editing panels. The authoritative scene lives on the spawned --edit
// server, so edits become commands sent back rather than local mutations.
class EditorApp {
public:
  struct LaunchOptions {
    std::string host = "127.0.0.1";
    int port = net::defaultPort;
    bool launchLocalServer = true;  // the editor edits a local project, so it spawns its own server
    std::string project;
  };

  explicit EditorApp(LaunchOptions options);

  ~EditorApp();

  [[nodiscard]] bool isActive() const;

  void run();

  void logMessage(const std::string& level, const std::string& message);

private:
  LaunchOptions m_options;

  std::shared_ptr<ManagedHost> m_host;
  std::unique_ptr<net::ServerProcess> m_serverProcess;
  std::shared_ptr<net::NetClient> m_netClient;

  std::shared_ptr<ComponentRegistry> m_componentRegistry;
  std::shared_ptr<AssetRegistry> m_assetRegistry;
  std::shared_ptr<SceneManager> m_sceneManager;
  std::shared_ptr<ProjectSerializer> m_projectSerializer;

  std::shared_ptr<vke::VulkanEngine> m_renderer;
  std::shared_ptr<GpuAssetCache> m_assetCache;
  std::shared_ptr<RenderSystem> m_renderSystem;

  std::shared_ptr<ComponentEditor> m_componentEditor;
  std::shared_ptr<ObjectGUIManager> m_objectGUIManager;
  std::shared_ptr<AssetBrowserPanel> m_assetBrowser;
  std::shared_ptr<SaveUI> m_saveUI;

  std::vector<std::string> m_errorMessages;
  std::string m_sceneViewName;
  bool m_shouldDisplayGui = true;

  // Only resend input when it changes (see ClientApp): keeps an unfocused editor from clobbering a
  // focused client's keys on the shared server-side InputState.
  std::vector<int> m_lastInputKeys;
  bool m_lastInputFocused = false;
  bool m_inputSent = false;

  vke::EventListener<vke::KeyCallbackEvent> m_keyCallbackEventListener;

  void createRenderer();

  void connectToServer();

  void registerEditors();

  void setupKeybinds();

  void applyMessage(const net::Message& message);

  void sendInput();

  void sendSceneControl(const std::string& op);

  void updateGui();

  void displayMenuBar();

  void displaySceneStatus();

  void updateDockSpace() const;

  void displayMessageLog();

  void variableUpdate();

  // TODO: the editor spawns a child ECS3DServer with --edit and an auth token, then connects via
  // TODO:   localhost as Role::editor. Edits become commands sent to the server, which owns the scene.

  static void setupImGuiStyle();
};



#endif //EDITORAPP_H
