#ifndef EDITORAPP_H
#define EDITORAPP_H

#include <VulkanEngine/components/window/Window.h>
#include <Protocol.h>
#include <scenes/SceneManager.h>
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
class ProjectPacker;
class RenderSystem;
class GpuAssetCache;
class ComponentEditor;
class ObjectGUIManager;
class AssetBrowserPanel;
class SaveUI;

namespace net {
  class NetClient;
  class ServerProcess;
  class Message;
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
    // The edit token presented at the handshake when attaching to an existing edit server (--host). A
    // spawned local server instead gets a fresh token generated at connect time.
    std::string authToken;
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

  // The token presented at the handshake to be authorized as Role::editor: generated per launch for a
  // spawned local server (and passed to it via --token), or taken from the launch options for --host.
  std::string m_authToken;

  std::shared_ptr<ComponentRegistry> m_componentRegistry;
  std::shared_ptr<AssetRegistry> m_assetRegistry;
  std::shared_ptr<SceneManager> m_sceneManager;
  std::shared_ptr<ProjectSerializer> m_projectSerializer;
  std::shared_ptr<ProjectPacker> m_projectPacker;

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

  // Whether the connected server accepts edits (from its editStatus message). False = read-only: the
  // editor still renders the scene for viewing but disables its editing UI and shows a cue. Defaults
  // true so the common case (the editor's own spawned --edit server) is unaffected if the message lags.
  bool m_serverEditable = true;

  SceneStatus m_sceneStatus = SceneStatus::running;

  // Only resend input when it changes (see ClientApp): keeps an unfocused editor from clobbering a
  // focused client's keys on the shared server-side InputState.
  std::vector<int> m_lastInputKeys;
  bool m_lastInputFocused = false;
  bool m_inputSent = false;

  // Edge-detect the mouse so viewport picking only fires on a fresh Ctrl+click.
  bool m_mouseWasPressed = false;

  vke::EventListener<vke::KeyCallbackEvent> m_keyCallbackEventListener;

  void createRenderer();

  void connectToServer();

  void registerEditors() const;

  void setupKeybinds();

  void applyMessage(const net::Message& message);

  void handleSnapshot(const net::Message& message) const;

  void handleStateDelta(const net::Message& message) const;

  void handleEditComponent(const net::Message& message) const;

  void handleEditStatus(const net::Message& message);

  void handleSceneStatus(const net::Message& message);

  void handlePicking();

  void sendInput();

  void sendSceneControl(const std::string& op) const;

  void updateGui();

  void displayMenuBar() const;

  void displaySceneStatus() const;

  void updateDockSpace() const;

  void displayMessageLog();

  void variableUpdate() const;

  // The editor spawns a child ECS3DServer (ServerProcess) in edit mode and connects over localhost as
  // Role::editor, authorized by the --token it shares with that server at the handshake; edits then
  // become commands the server applies.

  static void setupImGuiStyle();
};



#endif //EDITORAPP_H
