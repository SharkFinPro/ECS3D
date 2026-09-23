#ifndef EDITORAPP_H
#define EDITORAPP_H

#include <VulkanEngine/components/window/Window.h>
#include <Protocol.h>
#include <edits/EditHistory.h>
#include <scenes/SceneManager.h>
#include <uuid.h>
#include <nlohmann/json_fwd.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vke {
  class VulkanEngine;
}

class ManagedHost;
class Component;
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
class SettingsStore;
class SettingsPanel;
class ConsolePanel;
class RingBufferSink;
class KeybindTable;
class KeybindDispatcher;

namespace input {
  struct InputSnapshot;
}

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
    bool showServerConsole = true;  // whether the spawned local server gets its own console window
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

  // Sends the reverse of the top of the relevant stack through the normal send path (editComponent/
  // sceneEdit/asset messages, matching the command's payloadForm()) and logs a refusal instead when there
  // is nothing to send: an empty stack, a target that no longer matches what the command recorded, or a
  // non-reversible command kind (left on the stack rather than dropped - see
  // EditHistory::nextUndoIsReversible()). Called through requestUndo()/requestRedo() (see
  // EditorAppUndoMenu.cpp), which is what the Ctrl+Z/Ctrl+Shift+Z keybinds and the Edit menu actually
  // invoke - they add the in-flight gate documented there.
  void undo();

  void redo();

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
  // write it - the object tree, inspector, and asset browser. handlePicking writes it directly from
  // viewport mouse-picking.
  std::shared_ptr<EditorSelection> m_selection;

  std::shared_ptr<ComponentEditor> m_componentEditor;
  std::shared_ptr<ObjectGUIManager> m_objectGUIManager;
  std::shared_ptr<InspectorPanel> m_inspectorPanel;
  std::shared_ptr<AssetBrowserPanel> m_assetBrowser;
  std::shared_ptr<SaveUI> m_saveUI;

  // User preferences: per-user and per-machine, never project data, so this is read and written locally
  // rather than through the server. Built before the renderer, since the stored theme has to be on the
  // tokens before the first applyStyle().
  std::unique_ptr<SettingsStore> m_settings;
  std::unique_ptr<SettingsPanel> m_settingsPanel;

  // Feeds the Console panel: registered with Log so the panel can show script errors and server output
  // that would otherwise only reach stdout. Only the editor has a panel for it, so only the editor adds it.
  std::shared_ptr<RingBufferSink> m_consoleSink;
  std::unique_ptr<ConsolePanel> m_consolePanel;

  // Named editor actions mapped to key chords, dispatched independently of who handles them (SaveUI,
  // this class, or nothing yet - see setupKeybinds). Shared with SettingsPanel, which reads/rebinds them.
  std::shared_ptr<KeybindTable> m_keybindTable;
  std::shared_ptr<KeybindDispatcher> m_keybindDispatcher;

  // Every mutation this editor sends, recorded as it goes; undo()/redo() read it back and send the
  // reverse edit through the normal send path (see those methods). Ctrl+Z/Ctrl+Shift+Z and the Edit menu
  // call them through requestUndo()/requestRedo() (see EditorAppUndoMenu.cpp). Cleared wherever the
  // authored scene the recorded commands refer to is replaced: load project, scene switch, (re)connect,
  // play start/stop.
  edits::EditHistory m_editHistory;

  // See requestUndo()/requestRedo() in EditorAppUndoMenu.cpp: while true, a further undo/redo request is
  // ignored (and the Edit menu's items disabled) until the server's rebroadcast of the one already sent
  // lands (cleared in handleSnapshot/handleEditComponent) or this much time passes, whichever comes
  // first - undo validates against the editor's replicated view, which only updates on that rebroadcast,
  // so a second press inside one round trip would validate against a still-stale value.
  static constexpr std::chrono::milliseconds undoRedoPendingTimeout{500};
  bool m_undoRedoPending = false;
  std::chrono::steady_clock::time_point m_undoRedoPendingSince;

  std::vector<std::string> m_errorMessages;
  std::string m_sceneViewName;
  bool m_shouldDisplayGui = true;

  // Whether the connected server accepts edits (from its editStatus message). False = read-only: the
  // editor still renders the scene for viewing but disables its editing UI and shows a cue. Defaults
  // true so the common case (the editor's own spawned --edit server) is unaffected if the message lags.
  bool m_serverEditable = true;

  SceneStatus m_sceneStatus = SceneStatus::running;

  // What the server has actually reported, as opposed to m_sceneStatus's optimistic default: nullopt
  // until the first sceneStatus arrives, so that first message is not read as a start/stop transition.
  std::optional<SceneStatus> m_reportedSceneStatus;

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

  // Edge-detect the mouse so viewport picking only fires on a fresh click.
  bool m_mouseWasPressed = false;

  void createRenderer();

  void connectToServer();

  void registerEditors() const;

  void setupKeybinds();

  void setupObjectGUIManager();

  void setupInspectorPanel();

  void setupAssetBrowser();

  void setupSaveUI();

  void onAddAsset(const nlohmann::json& asset);

  void onRenameAsset(const uuids::uuid& assetUUID, const std::string& displayName);

  void onRemoveAsset(const uuids::uuid& assetUUID);

  // How many objects reference the asset by uuid, for the delete-confirmation warning.
  [[nodiscard]] int countAssetReferences(const uuids::uuid& assetUUID) const;

  void onEditComponent(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component) const;

  void onSceneEdit(const nlohmann::json& edit);

  void onEditCommitted(const uuids::uuid& objectUUID, const nlohmann::json& before, const nlohmann::json& after);

  void onLoadScene(const uuids::uuid& sceneUUID);

  void onUpdatePrefabBody(const uuids::uuid& assetUUID, const std::string& name, const std::string& body);

  void onLoadProject();

  void applyMessage(const net::Message& message);

  // Not const: clears the undo/redo in-flight gate (see m_undoRedoPending) as the rebroadcast a request
  // was waiting on.
  void handleSnapshot(const net::Message& message);

  void handleStateDelta(const net::Message& message) const;

  void handleEditComponent(const net::Message& message);

  void handleObjectSpawned(const net::Message& message) const;

  void handleObjectDestroyed(const net::Message& message) const;

  void handleObjectComponentsChanged(const net::Message& message) const;

  void handleEditStatus(const net::Message& message);

  void handleSceneStatus(const net::Message& message);

  // A batch of the server's own log entries (its own log, plus script output - both already reach the
  // server's Log). Writes them straight into m_consoleSink (rather than through Log::write) so each entry
  // keeps the timestamp it carried on the wire instead of being stamped with its arrival time.
  void handleServerLog(const net::Message& message) const;

  void handlePicking();

  // The input this view would report, with the keyboard and mouse gated as the editor UI requires.
  [[nodiscard]] input::InputSnapshot captureGatedInput() const;

  // Whether the snapshot differs from what was last sent (or nothing was sent yet).
  [[nodiscard]] bool hasInputChanged(const input::InputSnapshot& snapshot) const;

  void sendInput();

  void sendSceneControl(net::SceneControlOp op) const;

  // Shared tail of undo()/redo(): sends whichever payload the outcome carries through the normal send
  // path, or logs why nothing was sent. isUndo only picks the wording ("undo" vs. "redo") for the log.
  void reportHistoryOutcome(const edits::HistoryOutcome& outcome, bool isUndo);

  // What the Ctrl+Z/Ctrl+Shift+Z keybinds and the Edit menu actually call (EditorAppUndoMenu.cpp): a
  // no-op while undoRedoRequestBlocked(), otherwise calls undo()/redo() and starts the in-flight gate if
  // it actually sent something - see undoRedoPendingTimeout and EditorAppUndoMenu.cpp's gainedOneEntry().
  void requestUndo();

  void requestRedo();

  // True while a previously sent undo/redo request is still awaiting its rebroadcast and the gate's
  // timeout has not yet elapsed; clears the gate itself as a side effect once the timeout has elapsed, so
  // a caller need not poll it separately.
  [[nodiscard]] bool undoRedoRequestBlocked();

  void beginUndoRedoPending();

  // Called from handleSnapshot/handleEditComponent (the rebroadcast this gate is waiting on) and
  // wherever m_editHistory.clear() already is (load project, scene switch, reconnect, play start/stop) -
  // there is nothing left in flight to wait on once the history itself is gone.
  void clearUndoRedoPending();

  void updateGui();

  // Not const: unlike the rest of the menu bar, Edit's items call requestUndo()/requestRedo(), which
  // mutate the in-flight gate above.
  void displayMenuBar();

  // Between File and Window: "Undo <label>"/"Redo <label>" naming the next action (see
  // EditCommand::describeForMenu), disabled with nothing to act on, a read-only server, or a request
  // already in flight.
  void displayEditMenu();

  // One "Undo <label>"/"Redo <label>" menu item, split out of displayEditMenu() to keep that function's
  // branching down: isUndo picks the verb, the EditorAction the shortcut comes from, and which of
  // requestUndo()/requestRedo() a click invokes.
  void displayUndoRedoMenuItem(bool isUndo, const std::optional<std::string>& label, bool enabled);

  // The condition shared by the undo and redo items: something to act on, an editable server, and no
  // request already in flight.
  [[nodiscard]] bool canActOnHistoryItem(const std::optional<std::string>& label, bool requestInFlight) const;

  void displayWindowMenu() const;

  void displaySceneStatus();

  void displayPlayControls() const;

  void displayRayTracingToggle() const;

  void displaySceneReadout() const;

  // The "View" combo: the editor's free-fly camera, or any Camera in the scene (a client's player camera
  // is labelled with its slot).
  void displayCameraSelector();

  void updateDockSpace() const;

  void applyDockLocations() const;

  // False while the viewport has no usable size yet, so the caller retries on a later frame.
  [[nodiscard]] bool applyDockPercents() const;

  void displayMessageLog();

  void variableUpdate();

  // The editor spawns a child ECS3DServer (ServerProcess) in edit mode and connects over localhost as
  // Role::editor, authorized by the --token it shares with that server at the handshake; edits then
  // become commands the server applies.

  static void setupImGuiStyle();
};



#endif //EDITORAPP_H
