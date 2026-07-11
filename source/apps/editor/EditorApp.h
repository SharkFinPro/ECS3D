#ifndef EDITORAPP_H
#define EDITORAPP_H

#include <VulkanEngine/components/window/Window.h>
#include <Protocol.h>
#include <scenes/SceneManager.h>
#include <uuid.h>
#include <cstdint>
#include <memory>
#include <optional>
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
class InspectorPanel;
class AssetBrowserPanel;
class SaveUI;
class EditorSelection;

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

  // The editor-wide selection slot (object vs. asset vs. nothing), shared into the panels that read or
  // write it — the object tree, inspector, and asset browser. handlePicking writes it directly from
  // viewport mouse-picking.
  std::shared_ptr<EditorSelection> m_selection;

  std::shared_ptr<ComponentEditor> m_componentEditor;
  std::shared_ptr<ObjectGUIManager> m_objectGUIManager;
  std::shared_ptr<InspectorPanel> m_inspectorPanel;
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

  // The object whose Camera component the viewport looks through ("View" combo in Scene Status), letting
  // the editor see what a client sees. nullopt = the editor's own free-fly camera. Purely local: it's a
  // view choice, never replicated. Cleared when the chosen object leaves the scene or loses its Camera.
  std::optional<uuids::uuid> m_viewCameraObject;

  // Only resend input when it changes (see ClientApp): keeps an unfocused editor from clobbering a
  // focused client's keys on the shared server-side InputState.
  std::vector<int> m_lastInputKeys;
  bool m_lastInputFocused = false;
  uint8_t m_lastButtons = 0;
  float m_lastMouseX = 0.0f;
  float m_lastMouseY = 0.0f;
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

  void handleObjectSpawned(const net::Message& message) const;

  void handleObjectDestroyed(const net::Message& message) const;

  void handleEditStatus(const net::Message& message);

  void handleSceneStatus(const net::Message& message);

  void handlePicking();

  void sendInput();

  void sendSceneControl(net::SceneControlOp op) const;

  void updateGui();

  void displayMenuBar() const;

  void displaySceneStatus();

  // The "View" combo: the editor's free-fly camera, or any Camera in the scene (a client's player camera
  // is labelled with its slot).
  void displayCameraSelector();

  void updateDockSpace() const;

  void displayMessageLog();

  void variableUpdate();

  // The editor spawns a child ECS3DServer (ServerProcess) in edit mode and connects over localhost as
  // Role::editor, authorized by the --token it shares with that server at the handshake; edits then
  // become commands the server applies.

  static void setupImGuiStyle();
};



#endif //EDITORAPP_H
