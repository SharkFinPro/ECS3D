#include "EditorApp.h"
#include "DarkTitleBar.h"
#include <ComponentRegistry.h>
#include <ComponentRegistration.h>
#include <ProjectSerializer.h>
#include <ProjectPacker.h>
#include <assets/AssetRegistry.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <objects/Object.h>
#include <GpuAssetCache.h>
#include <RenderSystem.h>
#include <ComponentEditor.h>
#include <ObjectGUIManager.h>
#include <InspectorPanel.h>
#include <Selection.h>
#include <EditorTheme.h>
#include <AssetBrowserPanel.h>
#include <SaveUI.h>
#include <SettingsPanel.h>
#include <ConsolePanel.h>
#include <RingBufferSink.h>
#include <SettingsStore.h>
#include <Keybinds.h>
#include <EditorCameraSettings.h>
#include <KeybindDispatcher.h>
#include <objects/components/Component.h>
#include <objects/components/Camera.h>
#include <objects/components/PlayerController.h>
#include <components/TransformEditor.h>
#include <components/RigidBodyEditor.h>
#include <components/ModelRendererEditor.h>
#include <components/LightRendererEditor.h>
#include <components/ColliderEditor.h>
#include <components/ScriptEditor.h>
#include <components/PlayerControllerEditor.h>
#include <components/CameraEditor.h>
#include <NetClient.h>
#include <ServerProcess.h>
#include <ManagedHost.h>
#include <Log.h>
#include <LogSetup.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/camera/Camera.h>
#include <VulkanEngine/components/imGui/ImGuiInstance.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <chrono>
#include <exception>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <utility>

EditorApp::EditorApp(LaunchOptions options)
  : m_options(std::move(options)),
    m_host(std::make_shared<ManagedHost>()),
    m_componentRegistry(std::make_shared<ComponentRegistry>()),
    m_assetRegistry(std::make_shared<AssetRegistry>()),
    m_sceneManager(std::make_shared<SceneManager>())
{
  // Boot the CLR from the net transport's runtimeconfig (the editor only needs the socket assembly;
  // scripts run on the spawned --edit server).
  m_host->init("net/Transport");

  registerDataComponents(*m_componentRegistry);

  m_projectSerializer = std::make_shared<ProjectSerializer>(m_assetRegistry.get(), m_sceneManager.get(), m_componentRegistry);
  m_projectPacker = std::make_shared<ProjectPacker>(m_assetRegistry.get(), m_sceneManager.get(), m_componentRegistry);

  // Before createRenderer: the renderer runs setupImGuiStyle during construction, and that reads the
  // theme tokens, so a stored override has to be on them by then or the editor repaints on first sight.
  m_settings = std::make_unique<SettingsStore>(SettingsStore::defaultFile());
  SettingsPanel::applyStoredTheme(*m_settings);

  createRenderer();

  setupKeybinds();

  m_settingsPanel = std::make_unique<SettingsPanel>(*m_settings, m_renderer, m_keybindTable, m_keybindDispatcher);

  // Only the editor has a panel to show it, so only the editor registers the ring buffer - the console
  // and file sinks main() already registers keep receiving everything regardless.
  m_consoleSink = std::make_shared<RingBufferSink>(2000);
  Log::addSink(m_consoleSink);
  m_consolePanel = std::make_unique<ConsolePanel>(m_consoleSink);

  m_assetCache = std::make_shared<GpuAssetCache>(m_renderer, m_assetRegistry.get());
  m_renderSystem = std::make_shared<RenderSystem>();

  m_componentEditor = std::make_shared<ComponentEditor>();
  registerEditors();

  m_selection = std::make_shared<EditorSelection>();

  setupObjectGUIManager();
  setupInspectorPanel();
  setupAssetBrowser();
  setupSaveUI();

  // Save/Save As are wired to the table once SaveUI exists; toggleGui was already wired in setupKeybinds.
  m_keybindDispatcher->on(EditorAction::saveProject, [this] { static_cast<void>(m_saveUI->save()); });
  m_keybindDispatcher->on(EditorAction::saveProjectAs, [this] { m_saveUI->saveAs(); });

  m_netClient = std::make_shared<net::NetClient>(m_host);

  connectToServer();

  // Ask the server for the initial Snapshot.
  const net::Message message(net::MessageType::join);
  m_netClient->send(message);
}

void EditorApp::setupObjectGUIManager()
{
  // The object tree owns the hierarchy + structural tree edits + "Save as Prefab"; the Inspector owns
  // the selected item's body. Both read/write the one selection slot.
  m_objectGUIManager = std::make_shared<ObjectGUIManager>();
  m_objectGUIManager->setSelection(m_selection);
  m_objectGUIManager->setAddAssetCallback([this](const nlohmann::json& asset) { onAddAsset(asset); });
  m_objectGUIManager->setSceneEditCallback([this](const nlohmann::json& edit) { onSceneEdit(edit); });
  m_objectGUIManager->setSettings(m_settings.get());
}

void EditorApp::setupInspectorPanel()
{
  m_inspectorPanel = std::make_shared<InspectorPanel>(m_componentEditor, m_componentRegistry, m_assetCache);
  m_inspectorPanel->setSelection(m_selection);
  m_inspectorPanel->setAssetRegistry(m_assetRegistry.get());
  m_inspectorPanel->setEditCallback([this](const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component) {
    onEditComponent(objectUUID, component);
  });
  m_inspectorPanel->setEditCommittedCallback([this](const uuids::uuid& objectUUID, const nlohmann::json& before,
                                                    const nlohmann::json& after) {
    onEditCommitted(objectUUID, before, after);
  });
  m_inspectorPanel->setSceneEditCallback([this](const nlohmann::json& edit) { onSceneEdit(edit); });
  m_inspectorPanel->setLoadSceneCallback([this](const uuids::uuid& sceneUUID) { onLoadScene(sceneUUID); });
  m_inspectorPanel->setRenameAssetCallback([this](const uuids::uuid& assetUUID, const std::string& displayName) {
    onRenameAsset(assetUUID, displayName);
  });
  m_inspectorPanel->setRemoveAssetCallback([this](const uuids::uuid& assetUUID) { onRemoveAsset(assetUUID); });
  m_inspectorPanel->setAssetReferenceCountCallback([this](const uuids::uuid& assetUUID) {
    return countAssetReferences(assetUUID);
  });
  m_inspectorPanel->setUpdatePrefabBodyCallback([this](const uuids::uuid& assetUUID, const std::string& name,
                                                       const std::string& body) {
    onUpdatePrefabBody(assetUUID, name, body);
  });
}

void EditorApp::setupAssetBrowser()
{
  m_assetBrowser = std::make_shared<AssetBrowserPanel>(m_assetRegistry.get(), m_assetCache);
  m_assetBrowser->setSelection(m_selection);
  m_assetBrowser->setLoadSceneCallback([this](const uuids::uuid& sceneUUID) { onLoadScene(sceneUUID); });
  m_assetBrowser->setAddAssetCallback([this](const nlohmann::json& asset) { onAddAsset(asset); });
}

void EditorApp::setupSaveUI()
{
  m_saveUI = std::make_shared<SaveUI>(m_projectSerializer.get(), m_renderer);
  m_saveUI->setLoadProjectCallback([this] { onLoadProject(); });
}

void EditorApp::connectToServer()
{
  using namespace std::chrono_literals;

  // Whatever is on the stacks was recorded against the session being left; the join snapshot replaces
  // every object it named.
  m_editHistory.clear();
  clearUndoRedoPending();

  // The editor edits a local project, so (in singleplayer) it spawns its own edit-mode server gated by a
  // one-off token they share, then connects as Role::editor with that token. Attaching to an existing
  // server (--host) instead uses the token from the launch options.
  if (m_options.launchLocalServer)
  {
    // A fresh per-launch token so only this editor can edit the server it just spawned. The --edit flag
    // is the capability gate; the token additionally fends off another local process on loopback.
    std::mt19937 rng{ std::random_device{}() };
    m_authToken = uuids::to_string(uuids::uuid_random_generator{ rng }());

    m_serverProcess = std::make_unique<net::ServerProcess>();
    // --ephemeral makes the server exit when its last connection drops, so it can't outlive the editor.
    // Its own log file rather than the server's default: an editor and a client on one machine would
    // otherwise truncate and interleave the same one. This app's --log-file/--no-log-file are not
    // forwarded - the child's log is the child's. --token goes last, since it consumes what follows.
    const std::string arguments = "--edit --ephemeral " + logFileArgument("editor-server")
      + " --token " + m_authToken;
    if (!m_serverProcess->launch("ECS3DServer", arguments, m_options.showServerConsole))
    {
      Log::error(LogCategory::editor, "Failed to launch local server (ECS3DServer) next to this executable.");
    }
  }
  else
  {
    m_authToken = m_options.authToken;
  }

  // The (just-spawned) server needs a moment to boot the CLR and start listening, so retry.
  const auto deadline = std::chrono::steady_clock::now() + 15s;
  do
  {
    m_netClient->connect(m_options.host, m_options.port, net::Role::editor, m_authToken);

    if (m_netClient->isConnected())
    {
      return;
    }

    std::this_thread::sleep_for(250ms);
  }
  while (std::chrono::steady_clock::now() < deadline);

  Log::error(LogCategory::editor, "Could not connect to " + m_options.host + ":" + std::to_string(m_options.port) + ".");
}

EditorApp::~EditorApp()
{
  if (m_netClient)
  {
    m_netClient->disconnect();
  }

  if (m_host)
  {
    m_host->shutdown();
  }
}

bool EditorApp::isActive() const
{
  return m_renderer->isActive();
}

void EditorApp::run()
{
  while (isActive())
  {
    net::Message message;
    while (m_netClient->poll(message))
    {
      // A malformed message costs the message, not the session. Without this the exception escapes run()
      // and main exits, losing whatever was being edited.
      try
      {
        applyMessage(message);
      }
      catch (const std::exception& e)
      {
        logMessage("Error", std::string("Failed to apply a message from the server: ") + e.what());
      }
    }

    if (m_netClient->takeConnectionLost())
    {
      logMessage("Error", "Connection to the server was lost. Save your work and restart the editor.");
      m_serverEditable = false;
    }

    sendInput();

    handlePicking();

    m_settings->update();

    updateGui();

    variableUpdate();
  }
}

void EditorApp::createRenderer()
{
  // Unlike the client, the editor keeps the custom ImGui style.
  const vke::EngineConfig engineConfig {
    .window {
      .width = 1280,
      .height = 720,
      .title = "ECS3D Editor"
    },
    .camera {
      .position = { 0, 5, -50 }
    },
    .imGui {
      .maxTextures = 100,
      .styleSetup = setupImGuiStyle
    }
  };

  m_renderer = std::make_shared<vke::VulkanEngine>(engineConfig);

  // Overrides the engine-config default above with whatever the user last set, so a restart comes back
  // at the tuned speed rather than resetting to 1x.
  m_renderer->getCamera()->setSpeed(editorCameraSettings::readSpeed(*m_settings));

  m_sceneViewName = engineConfig.imGui.sceneViewName;

  // The title bar is native chrome ImGui never touches; follow the theme by luminance of the panel
  // token, the same surface the rest of the window is drawn on.
  const float panelLuminance = 0.299f * theme::panel.x + 0.587f * theme::panel.y + 0.114f * theme::panel.z;
  applyDarkTitleBar(m_renderer->getWindow()->getWindow(), panelLuminance < 0.5f);
}

void EditorApp::registerEditors() const
{
  registerTransformEditor(*m_componentEditor);
  registerRigidBodyEditor(*m_componentEditor);
  registerModelRendererEditor(*m_componentEditor, m_assetCache, m_assetRegistry.get());
  registerLightRendererEditor(*m_componentEditor);
  registerColliderEditors(*m_componentEditor);
  registerScriptEditor(*m_componentEditor);
  registerPlayerControllerEditor(*m_componentEditor);
  registerCameraEditor(*m_componentEditor);
}

void EditorApp::setupKeybinds()
{
  m_keybindTable = std::make_shared<KeybindTable>();
  m_keybindTable->load(*m_settings);

  m_keybindDispatcher = std::make_shared<KeybindDispatcher>(m_renderer, m_keybindTable);

  m_keybindDispatcher->on(EditorAction::toggleGui, [this] { m_shouldDisplayGui = !m_shouldDisplayGui; });

  // requestUndo()/requestRedo() (EditorAppUndoMenu.cpp) add the in-flight gate on top of undo()/redo() -
  // the Edit menu calls the same two methods.
  m_keybindDispatcher->on(EditorAction::undo, [this] { requestUndo(); });
  m_keybindDispatcher->on(EditorAction::redo, [this] { requestRedo(); });

  m_keybindDispatcher->on(EditorAction::deleteSelection, [this] { m_objectGUIManager->requestDeleteSelection(); });
  m_keybindDispatcher->on(EditorAction::duplicateSelection, [this] {
    const auto scene = m_sceneManager->getCurrentScene();
    m_objectGUIManager->duplicateSelection(scene ? scene->getObjectManager().get() : nullptr);
  });

  // Save/Save As are registered later, once m_saveUI exists. focus/gizmo stay in the table with no
  // handler - bindable and shown in Settings, but a no-op until a later feature gives them behavior.
}

void EditorApp::variableUpdate()
{
  const auto scene = m_sceneManager->getCurrentScene();
  const auto objectManager = scene ? scene->getObjectManager().get() : nullptr;

  if (objectManager)
  {
    m_renderSystem->variableUpdate(*objectManager, *m_assetCache, m_inspectorPanel->getHighlightUUIDs());
  }

  // Drop a stale choice (the object left the scene, or lost its Camera) rather than freezing the viewport
  // on that camera's last pose.
  if (objectManager && m_viewCameraObject)
  {
    const auto object = objectManager->getObjectByUUID(*m_viewCameraObject);

    if (!object || !object->getComponent<Camera>(ComponentType::camera))
    {
      m_viewCameraObject.reset();
    }
  }

  // Look through the selected scene camera, or hand the viewport back to the editor's free-fly camera.
  if (objectManager && m_viewCameraObject)
  {
    m_renderSystem->updateCamera(*objectManager, *m_assetCache, m_viewCameraObject);
  }
  else
  {
    m_renderSystem->useFreeFlyCamera(*m_assetCache);
  }

  m_renderer->render();
}

void EditorApp::setupImGuiStyle()
{
  ImGui::SetCurrentContext(vke::ImGuiInstance::getImGuiContext());

  theme::applyStyle();
}
