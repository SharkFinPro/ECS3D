#include "EditorApp.h"
#include <ComponentRegistry.h>
#include <ComponentRegistration.h>
#include <ProjectSerializer.h>
#include <ProjectPacker.h>
#include <Replication.h>
#include <assets/AssetRegistry.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <GpuAssetCache.h>
#include <RenderSystem.h>
#include <InputCapture.h>
#include <ComponentEditor.h>
#include <ObjectGUIManager.h>
#include <InspectorPanel.h>
#include <Selection.h>
#include <EditorTheme.h>
#include <GuiComponents.h>
#include <AssetBrowserPanel.h>
#include <SaveUI.h>
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
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/imGui/ImGuiInstance.h>
#include <VulkanEngine/components/renderingManager/RenderingManager.h>
#include <VulkanEngine/components/renderingManager/renderer3D/Renderer3D.h>
#include <VulkanEngine/components/renderingManager/renderer3D/MousePicker.h>
#include <objects/Object.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <chrono>
#include <iostream>
#include <optional>
#include <random>
#include <thread>

namespace {
  // How a camera reads in the editor's "View" combo: the owning object's name, the player slot when it's a
  // client's player camera, and a cue when the Camera is inactive (RenderSystem only renders through active
  // cameras, so selecting one shows the free-fly view instead).
  std::string cameraLabel(const std::shared_ptr<Object>& object)
  {
    std::string label = object->getName();

    if (const auto playerController = object->getComponent<PlayerController>(ComponentType::playerController))
    {
      label += " (Player " + std::to_string(playerController->getPlayerSlot()) + ")";
    }

    if (const auto camera = object->getComponent<Camera>(ComponentType::camera); camera && !camera->isActive())
    {
      label += " - inactive";
    }

    return label;
  }

  // Whether any of the object's components mention the asset uuid in their serialized form. Searching the
  // serialized form keeps this generic (no component type named here); only model/texture references -
  // the ones that would dangle - ever match.
  bool objectReferencesAsset(const std::shared_ptr<Object>& object, const std::string& uuidString)
  {
    for (const auto& [type, component] : object->getComponents())
    {
      if (component->serialize().dump().find(uuidString) != std::string::npos)
      {
        return true;
      }
    }

    return false;
  }

  // Count the object nodes within a serialized prefab body (one Object tree) that reference the uuid.
  int countReferencesInPrefabNode(const nlohmann::json& node, const std::string& uuidString)
  {
    int count = 0;

    if (const auto components = node.find("components"); components != node.end() && components->is_array())
    {
      for (const auto& component : *components)
      {
        if (component.dump().find(uuidString) != std::string::npos)
        {
          ++count;
          break;
        }
      }
    }

    if (const auto children = node.find("children"); children != node.end() && children->is_array())
    {
      for (const auto& child : *children)
      {
        if (child.is_object())
        {
          count += countReferencesInPrefabNode(child, uuidString);
        }
      }
    }

    return count;
  }
}

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

  createRenderer();

  m_assetCache = std::make_shared<GpuAssetCache>(m_renderer, m_assetRegistry.get());
  m_renderSystem = std::make_shared<RenderSystem>();

  m_componentEditor = std::make_shared<ComponentEditor>();
  registerEditors();

  // Register a new asset: apply locally for instant feedback, then on the server (which re-snapshots).
  // Shared by the asset browser's import/create and the object tree's "Save as Prefab".
  const auto addAsset = [this](const nlohmann::json& asset) {
    replication::applyAddAsset(*m_assetRegistry, *m_sceneManager, m_componentRegistry, asset);

    m_netClient->send(replication::packAddAsset(asset));
  };

  // Rename an asset (display-name override only). Same local-apply-then-send shape as addAsset.
  const auto renameAsset = [this](const uuids::uuid& assetUUID, const std::string& displayName) {
    const auto op = replication::buildRenameAsset(assetUUID, displayName);
    replication::applyRenameAsset(*m_assetRegistry, op);

    m_netClient->send(replication::packRenameAsset(op));
  };

  // Delete an asset: same local-apply-then-send shape. Deletion always succeeds; references dangle
  // (lookups null-tolerate a missing uuid).
  const auto removeAsset = [this](const uuids::uuid& assetUUID) {
    const auto op = replication::buildRemoveAsset(assetUUID);
    replication::applyRemoveAsset(*m_assetRegistry, op);

    m_netClient->send(replication::packRemoveAsset(op));
  };

  // How many objects reference an asset by uuid, for the delete-confirmation modal's warning. Scans the
  // replicated scenes' objects and every prefab body - the two places an object tree lives editor-side.
  const auto countAssetReferences = [this](const uuids::uuid& assetUUID) {
    const auto uuidString = uuids::to_string(assetUUID);
    int count = 0;

    for (const auto& [sceneUUID, scene] : m_sceneManager->getScenes())
    {
      const auto objectManager = scene->getObjectManager();
      if (!objectManager)
      {
        continue;
      }

      for (const auto& object : objectManager->getAllObjects())
      {
        if (objectReferencesAsset(object, uuidString))
        {
          ++count;
        }
      }
    }

    for (const auto& [recordUUID, record] : m_assetRegistry->getAssets())
    {
      if (record.type != AssetType::Prefab)
      {
        continue;
      }

      if (auto body = nlohmann::json::parse(record.body, nullptr, false); !body.is_discarded() && body.is_object())
      {
        count += countReferencesInPrefabNode(body, uuidString);
      }
    }

    return count;
  };

  // A component value edit: send the component's new state to the server. Only the Inspector fires this.
  const auto editComponent = [this](const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component) {
    const auto message = replication::buildComponentEdit(objectUUID, component);
    m_netClient->send(message);
  };

  // A structural change (add/remove/reparent object, add/remove component, rename, add script): the
  // server applies it and re-snapshots. Shared by the object tree and the Inspector.
  const auto sceneEdit = [this](const nlohmann::json& edit) {
    const auto payload = edit.dump();

    net::Message message(net::MessageType::sceneEdit);
    for (const std::vector<uint8_t> chunks(payload.begin(), payload.end()); const auto& chunk : chunks)
    {
      message.write(chunk);
    }
    m_netClient->send(message);
  };

  m_selection = std::make_shared<EditorSelection>();

  // The object tree owns the hierarchy + structural tree edits + "Save as Prefab"; the Inspector owns
  // the selected item's body. Both read/write the one selection slot.
  m_objectGUIManager = std::make_shared<ObjectGUIManager>();
  m_objectGUIManager->setSelection(m_selection);
  m_objectGUIManager->setAddAssetCallback(addAsset);
  m_objectGUIManager->setSceneEditCallback(sceneEdit);

  // Switch the active scene: apply locally for instant feedback, then tell the server (which re-snapshots).
  // Shared by the asset browser's scene double-click and the Inspector's "Load Scene" button.
  const auto loadScene = [this](const uuids::uuid& sceneUUID) {
    if (const auto scene = m_sceneManager->getScene(sceneUUID))
    {
      m_sceneManager->loadScene(scene);
    }

    net::Message message(net::MessageType::sceneControl);
    message.write(net::SceneControlOp::loadScene);
    message.writeString(uuids::to_string(sceneUUID));
    m_netClient->send(message);
  };

  // A prefab body edit: re-register under the existing name, updating the body in place and keeping the
  // uuid - the same path "Save as Prefab" over an existing name takes, via the same addAsset shape.
  const auto updatePrefabBody = [addAsset](const uuids::uuid& assetUUID, const std::string& name,
                                           const std::string& body) {
    addAsset({
      { "assetType", "prefab" },
      { "uuid", uuids::to_string(assetUUID) },
      { "name", name },
      { "body", body }
    });
  };

  m_inspectorPanel = std::make_shared<InspectorPanel>(m_componentEditor, m_componentRegistry, m_assetCache);
  m_inspectorPanel->setSelection(m_selection);
  m_inspectorPanel->setAssetRegistry(m_assetRegistry.get());
  m_inspectorPanel->setEditCallback(editComponent);
  m_inspectorPanel->setSceneEditCallback(sceneEdit);
  m_inspectorPanel->setLoadSceneCallback(loadScene);
  m_inspectorPanel->setRenameAssetCallback(renameAsset);
  m_inspectorPanel->setRemoveAssetCallback(removeAsset);
  m_inspectorPanel->setAssetReferenceCountCallback(countAssetReferences);
  m_inspectorPanel->setUpdatePrefabBodyCallback(updatePrefabBody);

  m_assetBrowser = std::make_shared<AssetBrowserPanel>(m_assetRegistry.get(), m_assetCache);
  m_assetBrowser->setSelection(m_selection);
  m_assetBrowser->setLoadSceneCallback(loadScene);
  m_assetBrowser->setAddAssetCallback(addAsset);

  m_saveUI = std::make_shared<SaveUI>(m_projectSerializer.get(), m_renderer);
  m_saveUI->setLoadProjectCallback([this] {
    // Open/New: the server owns the running sim, so send it the packed project (SaveUI already applied it
    // to our managers); it reloads and re-snapshots.
    net::Message message(net::MessageType::loadProject);
    m_projectPacker->pack(message);

    std::cerr << "[Editor] Sending loadProject (" << message.size() << " bytes) to server." << std::endl;

    m_netClient->send(message);
  });

  setupKeybinds();

  m_netClient = std::make_shared<net::NetClient>(m_host);

  connectToServer();

  // Ask the server for the initial Snapshot.
  const net::Message message(net::MessageType::join);
  m_netClient->send(message);
}

void EditorApp::connectToServer()
{
  using namespace std::chrono_literals;

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
    const std::string arguments = "--edit --ephemeral --token " + m_authToken;
    if (!m_serverProcess->launch("ECS3DServer", arguments))
    {
      std::cerr << "[Editor] Failed to launch local server (ECS3DServer) next to this executable." << std::endl;
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

  std::cerr << "[Editor] Could not connect to " << m_options.host << ":" << m_options.port << "." << std::endl;
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
      applyMessage(message);
    }

    sendInput();

    handlePicking();

    updateGui();

    variableUpdate();
  }
}

void EditorApp::handlePicking()
{
  const auto scene = m_sceneManager->getCurrentScene();
  if (!scene)
  {
    return;
  }

  const auto window = m_renderer->getWindow();
  const bool pressed = window->buttonIsPressed(GLFW_MOUSE_BUTTON_LEFT);

  // Select on a fresh Ctrl+Left-click over the viewport. isSelected() is the renderer's pick result
  // from last frame.
  const auto mousePicker = m_renderer->getRenderingManager()->getRenderer3D()->getMousePicker();
  if (!m_mouseWasPressed && pressed && window->keyIsPressed(GLFW_KEY_LEFT_CONTROL) && mousePicker->canMousePick())
  {
    std::optional<uuids::uuid> picked;
    for (const auto& object : scene->getObjectManager()->getAllObjects())
    {
      if (m_renderSystem->isSelected(object->getUUID()))
      {
        picked = object->getUUID();
        break;
      }
    }

    // Written directly to the shared selection the object tree/inspector read.
    if (picked.has_value())
    {
      m_selection->selectObject(picked.value());
    }
    else
    {
      m_selection->clear();
    }
  }

  m_mouseWasPressed = pressed;
}

void EditorApp::sendInput()
{
  const auto& io = ImGui::GetIO();

  // Don't let editor UI typing drive the game: when ImGui wants the keyboard, report no keys. focused
  // stays true so this view never disables another connected view's input.
  input::InputSnapshot snapshot = input::capture(*m_renderer);

  if (io.WantCaptureKeyboard)
  {
    snapshot.keys.clear();
  }

  // The mouse can't be gated on io.WantCaptureMouse: the viewport is itself an ImGui window, so that
  // flag is set whenever the cursor is over it, which would swallow the right-drag mouse-look a script
  // reads. Forward the mouse only while looking through a scene camera (in free-fly the right-drag is
  // the editor's own camera) and while the scene view is focused.
  const bool mouseDrivesGame = m_viewCameraObject.has_value()
                            && m_renderer->getRenderingManager()->isSceneFocused();

  if (!mouseDrivesGame)
  {
    snapshot.mouseDeltaX = 0.0f;
    snapshot.mouseDeltaY = 0.0f;
    snapshot.scrollY = 0.0f;
    snapshot.buttons = 0;
  }

  const bool discreteChanged = !m_inputSent
    || snapshot.keys != m_lastInputKeys
    || snapshot.focused != m_lastInputFocused
    || snapshot.buttons != m_lastButtons
    || snapshot.mouseX != m_lastMouseX
    || snapshot.mouseY != m_lastMouseY;

  const bool scrolled = snapshot.scrollY != 0.0f;

  if (m_inputSent && !discreteChanged && !scrolled)
  {
    return;
  }

  m_lastInputKeys = snapshot.keys;
  m_lastInputFocused = snapshot.focused;
  m_lastButtons = snapshot.buttons;
  m_lastMouseX = snapshot.mouseX;
  m_lastMouseY = snapshot.mouseY;
  m_inputSent = true;

  net::Message message(net::MessageType::inputState);
  message.write(snapshot.focused);
  message.write(snapshot.keys.size());
  for (const auto& key : snapshot.keys)
  {
    message.write(key);
  }

  message.write(snapshot.mouseX);
  message.write(snapshot.mouseY);
  message.write(snapshot.mouseDeltaX);
  message.write(snapshot.mouseDeltaY);
  message.write(snapshot.scrollY);
  message.write(snapshot.buttons);

  m_netClient->send(message);
}

void EditorApp::sendSceneControl(const net::SceneControlOp op) const
{
  net::Message message(net::MessageType::sceneControl);
  message.write(op);
  m_netClient->send(message);
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

  m_sceneViewName = engineConfig.imGui.sceneViewName;
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
  m_keyCallbackEventListener = m_renderer->getWindow()->on<vke::KeyCallbackEvent>([this](const vke::KeyCallbackEvent& e) {
    if (e.action != GLFW_PRESS)
    {
      return;
    }

    if (e.key == GLFW_KEY_F10)
    {
      m_shouldDisplayGui = !m_shouldDisplayGui;
    }
  });
}

void EditorApp::applyMessage(const net::Message& message)
{
  switch (message.getType())
  {
    case net::MessageType::snapshot:
      handleSnapshot(message);
      break;

    case net::MessageType::stateDelta:
      handleStateDelta(message);
      break;

    case net::MessageType::editComponent:
      handleEditComponent(message);
      break;

    case net::MessageType::objectSpawned:
      handleObjectSpawned(message);
      break;

    case net::MessageType::objectDestroyed:
      handleObjectDestroyed(message);
      break;

    case net::MessageType::editStatus:
      handleEditStatus(message);
      break;

    case net::MessageType::sceneStatus:
      handleSceneStatus(message);
      break;

    default: break;
  }
}

void EditorApp::handleSnapshot(const net::Message& message) const
{
  // Full state on join: rebuild the replicated scene from the packed project blob.
  m_projectPacker->unpack(message);

  const auto scene = m_sceneManager->getCurrentScene();
  std::cerr << "[Editor] Applied snapshot (" << message.size() << " bytes). Current scene: "
            << (scene ? scene->getName() : "<none>") << " ("
            << (scene ? scene->getObjectManager()->getAllObjects().size() : 0) << " objects)." << std::endl;
}

void EditorApp::handleStateDelta(const net::Message& message) const
{
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    replication::unpackStateDelta(*scene->getObjectManager(), message);
  }
}

void EditorApp::handleEditComponent(const net::Message& message) const
{
  // Another editor (or this one, echoed by the server) changed a component.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    replication::applyComponentEdit(*scene->getObjectManager(), message);
  }
}

void EditorApp::handleObjectSpawned(const net::Message& message) const
{
  // A script spawned an object at runtime; splice the packed object into the replicated scene.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    replication::applyObjectSpawned(*scene->getObjectManager(), message);
  }
}

void EditorApp::handleObjectDestroyed(const net::Message& message) const
{
  // A script destroyed an object at runtime; drop it from the replicated scene.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    replication::applyObjectDestroyed(*scene->getObjectManager(), message);
  }
}

void EditorApp::handleEditStatus(const net::Message& message)
{
  net::MessageReader reader(message);

  // The server told us whether it's editable; a non-edit server makes the editor a read-only viewer.
  m_serverEditable = reader.read<bool>();

  if (!m_serverEditable)
  {
    logMessage("Info", "Connected to a non-edit server - the editor is read-only.");
  }
}

void EditorApp::handleSceneStatus(const net::Message& message)
{
  net::MessageReader reader(message);
  m_sceneStatus = reader.read<SceneStatus>();
}

void EditorApp::updateGui()
{
  if (!m_shouldDisplayGui)
  {
    return;
  }

  // Propagate the server's editability so the panels disable their mutating affordances (and show a
  // read-only cue) on a non-edit server.
  m_objectGUIManager->setEditable(m_serverEditable);
  m_inspectorPanel->setEditable(m_serverEditable);
  m_assetBrowser->setEditable(m_serverEditable);
  m_saveUI->setEditable(m_serverEditable);

  displayMenuBar();

  updateDockSpace();

  m_assetBrowser->displayGui();

  displayMessageLog();

  displaySceneStatus();

  // The object tree + Inspector panels (always drawn so they stay present/dockable; empty when no scene
  // is loaded yet). Edits fire the callbacks wired in the ctor.
  const auto scene = m_sceneManager->getCurrentScene();
  const auto* objectManager = scene ? scene->getObjectManager().get() : nullptr;
  const auto activeSceneUUID = scene ? std::optional(scene->getUUID()) : std::nullopt;
  m_objectGUIManager->displayGui(objectManager);
  m_inspectorPanel->displayGui(objectManager, activeSceneUUID);

  // Scenes are browsed/switched from the "Assets" panel (double-click a scene tile), not a separate
  // scene-selector widget.
}

void EditorApp::displayMenuBar() const
{
  if (ImGui::BeginMainMenuBar())
  {
    m_renderer->getImGuiInstance()->setMenuBarHeight(ImGui::GetWindowSize().y);

    if (ImGui::BeginMenu("File"))
    {
      // Save serializes the replicated project to disk; New/Open send it to the server (which reloads +
      // re-snapshots). New/Open are mutations, disabled on a read-only server; Save stays available.
      ImGui::BeginDisabled(!m_serverEditable);
      if (ImGui::MenuItem("New"))
      {
        m_saveUI->createNewProject();
      }

      if (ImGui::MenuItem("Open"))
      {
        m_saveUI->open();
      }
      ImGui::EndDisabled();

      ImGui::Separator();

      if (ImGui::MenuItem("Save", "Ctrl+S"))
      {
        m_saveUI->save();
      }

      if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
      {
        m_saveUI->saveAs();
      }

      ImGui::EndMenu();
    }

    m_assetBrowser->displayMenuWidget();

    // A persistent read-only badge, right-aligned, whenever the connected server isn't in edit mode.
    if (!m_serverEditable)
    {
      const char* badge = "READ-ONLY (server not in edit mode)";
      const float badgeWidth = ImGui::CalcTextSize(badge).x;
      ImGui::SameLine(ImGui::GetWindowWidth() - badgeWidth - ImGui::GetStyle().WindowPadding.x * 2.0f);
      ImGui::TextColored(theme::scriptAmber, "%s", badge);
    }

    ImGui::EndMainMenuBar();
  }
}

void EditorApp::updateDockSpace() const
{
  static bool dockPercentsSetup = false;
  static bool dockLocationsSetup = false;

  if (!dockLocationsSetup && dockPercentsSetup)
  {
    const auto gui = m_renderer->getImGuiInstance();

    gui->dockCenter(m_sceneViewName.c_str());

    gui->dockLeft("Objects");

    gui->dockRight("Inspector");

    gui->dockTop("Scene Status");

    gui->dockBottom("Assets");
    gui->dockBottom("Project Errors");

    dockLocationsSetup = true;
  }

  if (!dockPercentsSetup)
  {
    const auto gui = m_renderer->getImGuiInstance();

    gui->setTopDockPercent(0.09);
    gui->setBottomDockPercent(0.28);

    gui->setLeftDockPercent(0.2);
    gui->setRightDockPercent(0.35);

    dockPercentsSetup = true;
  }
}

void EditorApp::displayMessageLog()
{
  ImGui::Begin("Project Errors");

  if (m_errorMessages.empty())
  {
    // Mockup's reassuring empty state instead of a bare, blank panel.
    gc::successEmptyState("No problems detected");
    ImGui::End();
    return;
  }

  // Count badge + Clear on the header row.
  gc::pill(std::to_string(m_errorMessages.size()).c_str(), theme::t3);
  ImGui::SameLine();
  if (ImGui::Button("Clear"))
  {
    m_errorMessages.clear();
  }

  ImGui::Spacing();

  for (const auto& message : m_errorMessages)
  {
    ImGui::TextWrapped("%s", message.c_str());
  }

  ImGui::End();
}

void EditorApp::displaySceneStatus()
{
  constexpr int sceneStatusButtonWidth = 125;

  ImGui::Begin("Scene Status");

  // Play controls first (mockup's leading accent Start button).
  if (m_sceneStatus != SceneStatus::running)
  {
    ImGui::BeginDisabled(!m_serverEditable);
    ImGui::PushStyleColor(ImGuiCol_Button, theme::accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::v4(60, 200, 224));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::v4(60, 200, 224));
    ImGui::PushStyleColor(ImGuiCol_Text, theme::onAcc);
    if (ImGui::Button("Start", {sceneStatusButtonWidth, 0}))
    {
      sendSceneControl(net::SceneControlOp::start);
    }
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
  }
  else
  {
    ImGui::BeginDisabled(!m_serverEditable);
    if (ImGui::Button("Pause", {sceneStatusButtonWidth, 0}))
    {
      sendSceneControl(net::SceneControlOp::pause);
    }
    ImGui::EndDisabled();
  }

  if (m_sceneStatus != SceneStatus::stopped)
  {
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_serverEditable);
    if (ImGui::Button("Stop", {sceneStatusButtonWidth, 0}))
    {
      sendSceneControl(net::SceneControlOp::stop);
    }
    ImGui::EndDisabled();
  }

  // Ray tracing toggle: a local render setting (this view's vke renderer only, never replicated), so
  // it stays enabled on a read-only server. Greyed out when the device can't ray trace.
  const auto renderingManager = m_renderer->getRenderingManager();
  ImGui::SameLine(0.0f, 18.0f);
  ImGui::BeginDisabled(!renderingManager->supportsRayTracing());
  bool rayTracing = renderingManager->isRayTracingEnabled();
  if (gc::accentCheckboxCompact("Ray Tracing", &rayTracing))
  {
    if (rayTracing)
    {
      renderingManager->enableRayTracing();
    }
    else
    {
      renderingManager->disableRayTracing();
    }
  }
  ImGui::EndDisabled();

  displayCameraSelector();

  // Status readout (divider + dot/label + scene name), right-aligned to the panel edge so the play
  // buttons stay put on the left regardless of the readout's width.
  const char* label = m_sceneStatus == SceneStatus::running ? "Running"
                    : m_sceneStatus == SceneStatus::paused  ? "Paused"
                                                            : "Stopped";
  const ImVec4 dotCol = m_sceneStatus == SceneStatus::running ? theme::sceneGreen
                      : m_sceneStatus == SceneStatus::paused  ? theme::scriptAmber
                                                              : theme::t3;
  const auto scene = m_sceneManager->getCurrentScene();

  constexpr float nameGap = 14.0f;     // scene name -> divider
  constexpr float dividerGap = 14.0f;  // divider -> dot block
  constexpr float dotToLabel = 18.0f;  // dot block start -> label text
  float readoutWidth = dividerGap + dotToLabel + ImGui::CalcTextSize(label).x;
  if (scene)
  {
    readoutWidth += ImGui::CalcTextSize(scene->getName().c_str()).x + nameGap;
  }

  // Anchor to the right edge, but never overlap the play buttons on a narrow panel.
  ImGui::SameLine();
  const float readoutX = std::max(ImGui::GetCursorPosX() + 4.0f,
                                  ImGui::GetContentRegionMax().x - readoutWidth);
  ImGui::SetCursorPosX(readoutX);

  // Current scene name (muted) first, mirroring the mockup's toolbar.
  if (scene)
  {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::t3, "%s", scene->getName().c_str());
    ImGui::SameLine(0.0f, nameGap);
  }

  // Divider between the scene name and the status readout.
  const ImVec2 dp = ImGui::GetCursorScreenPos();
  const float frameH = ImGui::GetFrameHeight();
  ImGui::GetWindowDrawList()->AddLine(ImVec2(dp.x, dp.y + frameH * 0.2f),
                                      ImVec2(dp.x, dp.y + frameH * 0.8f), theme::u32(theme::line));
  ImGui::SetCursorScreenPos(ImVec2(dp.x + dividerGap, dp.y));

  // Status indicator dot + label (green running / amber paused / muted stopped).
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float cy = p.y + ImGui::GetFrameHeight() * 0.5f;
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 5.0f, cy), 4.0f, theme::u32(dotCol));
    ImGui::SetCursorScreenPos(ImVec2(p.x + dotToLabel, p.y));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::t2, "%s", label);
  }

  ImGui::End();
}

void EditorApp::displayCameraSelector()
{
  constexpr auto freeFlyLabel = "Editor Camera";

  const auto scene = m_sceneManager->getCurrentScene();
  const auto objectManager = scene ? scene->getObjectManager().get() : nullptr;

  // A view choice, not a scene edit, so this stays enabled on a read-only server.
  std::string preview = freeFlyLabel;
  if (objectManager && m_viewCameraObject)
  {
    if (const auto object = objectManager->getObjectByUUID(*m_viewCameraObject))
    {
      preview = cameraLabel(object);
    }
  }

  ImGui::SameLine(0.0f, 18.0f);
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(theme::t2, "%s", "View");
  ImGui::SameLine();

  ImGui::SetNextItemWidth(200.0f);
  if (ImGui::BeginCombo("##ViewCamera", preview.c_str()))
  {
    if (ImGui::Selectable(freeFlyLabel, !m_viewCameraObject))
    {
      m_viewCameraObject.reset();
    }

    if (objectManager)
    {
      for (const auto& object : objectManager->getAllObjects())
      {
        if (!object->getComponent<Camera>(ComponentType::camera))
        {
          continue;
        }

        const auto uuid = object->getUUID();

        // Objects can share a name, so the uuid disambiguates the ImGui id.
        const std::string label = cameraLabel(object) + "##" + uuids::to_string(uuid);

        if (ImGui::Selectable(label.c_str(), m_viewCameraObject == uuid))
        {
          m_viewCameraObject = uuid;
        }
      }
    }

    ImGui::EndCombo();
  }
}

void EditorApp::variableUpdate()
{
  const auto scene = m_sceneManager->getCurrentScene();
  const auto objectManager = scene ? scene->getObjectManager().get() : nullptr;

  if (objectManager)
  {
    m_renderSystem->variableUpdate(*objectManager, *m_assetCache, m_inspectorPanel->getHighlightUUID());
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

void EditorApp::logMessage(const std::string& level, const std::string& message)
{
  m_errorMessages.push_back("[" + level + "] " + message);
}

void EditorApp::setupImGuiStyle()
{
  ImGui::SetCurrentContext(vke::ImGuiInstance::getImGuiContext());

  theme::applyStyle();
}
