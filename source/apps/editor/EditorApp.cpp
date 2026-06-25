#include "EditorApp.h"
#include <ComponentRegistry.h>
#include <ComponentRegistration.h>
#include <ProjectSerializer.h>
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
#include <AssetBrowserPanel.h>
#include <SaveUI.h>
#include <objects/components/Component.h>
#include <components/TransformEditor.h>
#include <components/RigidBodyEditor.h>
#include <components/ModelRendererEditor.h>
#include <components/LightRendererEditor.h>
#include <components/ColliderEditor.h>
#include <components/ScriptEditor.h>
#include <NetClient.h>
#include <ServerProcess.h>
#include <ManagedHost.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/imGui/ImGuiInstance.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <thread>

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

  createRenderer();

  m_assetCache = std::make_shared<GpuAssetCache>(m_renderer, m_assetRegistry.get());
  m_renderSystem = std::make_shared<RenderSystem>();

  m_componentEditor = std::make_shared<ComponentEditor>();
  registerEditors();

  m_objectGUIManager = std::make_shared<ObjectGUIManager>(m_componentEditor);
  m_objectGUIManager->setEditCallback([this](const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component) {
    // A widget changed: send the component's new state to the authoritative server as an edit command.
    const auto payload = replication::buildComponentEdit(objectUUID, component).dump();

    m_netClient->send(net::Message{
      .type = net::MessageType::editComponent,
      .payload = std::vector<uint8_t>(payload.begin(), payload.end())
    });
  });
  m_objectGUIManager->setSceneEditCallback([this](const nlohmann::json& edit) {
    // A structural change (add/remove object or component): the server applies it and re-snapshots.
    const auto payload = edit.dump();

    m_netClient->send(net::Message{
      .type = net::MessageType::sceneEdit,
      .payload = std::vector<uint8_t>(payload.begin(), payload.end())
    });
  });

  m_assetBrowser = std::make_shared<AssetBrowserPanel>(m_assetRegistry.get());

  m_saveUI = std::make_shared<SaveUI>(m_projectSerializer.get(), m_renderer);
  m_saveUI->setLoadProjectCallback([this](const std::string& projectJson) {
    // Open/New: the server owns the scene, so send it the project blob; it reloads and re-snapshots.
    m_netClient->send(net::Message{
      .type = net::MessageType::loadProject,
      .payload = std::vector<uint8_t>(projectJson.begin(), projectJson.end())
    });
  });

  setupKeybinds();

  m_netClient = std::make_shared<net::NetClient>(m_host);

  connectToServer();

  // Ask the server for the initial Snapshot.
  m_netClient->send(net::Message{ .type = net::MessageType::join, .payload = {} });
}

void EditorApp::connectToServer()
{
  using namespace std::chrono_literals;

  // The editor edits a local project, so (in singleplayer) it spawns its own server. TODO: pass --edit
  // + an auth token once the server enforces the edit gate; for now it connects as Role::editor.
  if (m_options.launchLocalServer)
  {
    m_serverProcess = std::make_unique<net::ServerProcess>();
    if (!m_serverProcess->launch("ECS3DServer"))
    {
      std::cerr << "[Editor] Failed to launch local server (ECS3DServer) next to this executable." << std::endl;
    }
  }

  // The (just-spawned) server needs a moment to boot the CLR and start listening, so retry.
  const auto deadline = std::chrono::steady_clock::now() + 15s;
  do
  {
    m_netClient->connect(m_options.host, m_options.port, net::Role::editor, "");

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

    updateGui();

    variableUpdate();
  }
}

void EditorApp::sendInput()
{
  // Don't let typing in an editor panel drive the player: when ImGui wants the keyboard, report no
  // keys. focused stays true (an unfocused window already reports no keys), so this view never disables
  // another connected view's input on the shared server-side InputState.
  input::InputSnapshot snapshot;
  if (!ImGui::GetIO().WantCaptureKeyboard)
  {
    snapshot = input::capture(*m_renderer);
  }

  if (m_inputSent && snapshot.keys == m_lastInputKeys && snapshot.focused == m_lastInputFocused)
  {
    return;
  }

  m_lastInputKeys = snapshot.keys;
  m_lastInputFocused = snapshot.focused;
  m_inputSent = true;

  const nlohmann::json payload = {
    { "keys", snapshot.keys },
    { "focused", snapshot.focused }
  };

  const auto dumped = payload.dump();

  m_netClient->send(net::Message{
    .type = net::MessageType::inputState,
    .payload = std::vector<uint8_t>(dumped.begin(), dumped.end())
  });
}

void EditorApp::sendSceneControl(const std::string& op)
{
  const nlohmann::json payload = { { "op", op } };

  const auto dumped = payload.dump();

  m_netClient->send(net::Message{
    .type = net::MessageType::sceneControl,
    .payload = std::vector<uint8_t>(dumped.begin(), dumped.end())
  });
}

void EditorApp::createRenderer()
{
  // Ported from ECS3D::initRenderer. Unlike the client, the editor keeps the custom ImGui style.
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

void EditorApp::registerEditors()
{
  // The per-type editing widgets, keyed by component type string (the central componentEditor the old
  // per-component displayGui() bodies move into).
  registerTransformEditor(*m_componentEditor);
  registerRigidBodyEditor(*m_componentEditor);
  registerModelRendererEditor(*m_componentEditor);
  registerLightRendererEditor(*m_componentEditor);
  registerColliderEditors(*m_componentEditor);
  registerScriptEditor(*m_componentEditor);
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
  const std::string payload(message.payload.begin(), message.payload.end());

  const auto json = nlohmann::json::parse(payload, nullptr, false);
  if (json.is_discarded())
  {
    return;
  }

  switch (message.type)
  {
    case net::MessageType::snapshot:
    {
      // Full state on join: rebuild the replicated scene from the project blob.
      m_projectSerializer->deserialize(json);

      const auto scene = m_sceneManager->getCurrentScene();
      std::cerr << "[Editor] Applied snapshot (" << message.payload.size() << " bytes). Current scene: "
                << (scene ? scene->getName() : "<none>") << " ("
                << (scene ? scene->getObjectManager()->getAllObjects().size() : 0) << " objects)." << std::endl;
      break;
    }
    case net::MessageType::stateDelta:
      if (const auto scene = m_sceneManager->getCurrentScene())
      {
        replication::applyStateDelta(*scene->getObjectManager(), json);
      }
      break;
    case net::MessageType::editComponent:
      // Another editor (or this one, echoed by the server) changed a component.
      if (const auto scene = m_sceneManager->getCurrentScene())
      {
        replication::applyComponentEdit(*scene->getObjectManager(), json);
      }
      break;
    default:
      break;
  }
}

void EditorApp::updateGui()
{
  if (!m_shouldDisplayGui)
  {
    return;
  }

  displayMenuBar();

  updateDockSpace();

  displayMessageLog();

  displaySceneStatus();

  m_assetBrowser->displayGui();

  // The object tree + selected-object component panels (always drawn so they stay present/dockable;
  // empty when no scene is loaded yet). Edits fire the callbacks wired in the ctor.
  const auto scene = m_sceneManager->getCurrentScene();
  m_objectGUIManager->displayGui(scene ? scene->getObjectManager().get() : nullptr);

  // TODO: the Scene Status start/pause/stop panel (a server lifecycle command) and the "Scenes" list.
}

void EditorApp::displayMenuBar()
{
  if (ImGui::BeginMainMenuBar())
  {
    m_renderer->getImGuiInstance()->setMenuBarHeight(ImGui::GetWindowSize().y);

    if (ImGui::BeginMenu("File"))
    {
      // Save serializes the replicated project to disk; New/Open send the project to the server (which
      // reloads + re-snapshots), since the server owns the scene.
      if (ImGui::MenuItem("New"))
      {
        m_saveUI->createNewProject();
      }

      if (ImGui::MenuItem("Open"))
      {
        m_saveUI->open();
      }

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
    gui->dockLeft("Scenes");

    gui->dockRight("Selected Object");

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

  if (ImGui::Button("Clear"))
  {
    m_errorMessages.clear();
  }

  for (const auto& message : m_errorMessages)
  {
    ImGui::TextWrapped("%s", message.c_str());
  }

  ImGui::End();
}

void EditorApp::displaySceneStatus()
{
  ImGui::Begin("Scene Status");

  // The scene lives on the authoritative server, so these are fire-and-forget lifecycle commands. The
  // server applies them and re-snapshots (it doesn't replicate the running/paused status back yet, so
  // the buttons aren't disabled by state).
  if (ImGui::Button("Start"))
  {
    sendSceneControl("start");
  }

  ImGui::SameLine();
  if (ImGui::Button("Pause"))
  {
    sendSceneControl("pause");
  }

  ImGui::SameLine();
  if (ImGui::Button("Stop"))
  {
    sendSceneControl("stop");
  }

  ImGui::End();
}

void EditorApp::variableUpdate()
{
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    m_renderSystem->variableUpdate(*scene->getObjectManager(), *m_assetCache);
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

  // Future Dark style reworked from ImThemes (ported from ECS3D::setupImGuiStyle).
  ImGuiStyle& style = ImGui::GetStyle();

  style.Alpha = 1.0f;
  style.DisabledAlpha = 1.0f;
  style.WindowPadding = ImVec2(12.0f, 12.0f);
  style.WindowRounding = 0.0f;
  style.WindowBorderSize = 0.0f;
  style.WindowMinSize = ImVec2(20.0f, 20.0f);
  style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
  style.WindowMenuButtonPosition = ImGuiDir_None;
  style.ChildRounding = 0.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupRounding = 0.0f;
  style.PopupBorderSize = 1.0f;
  style.FramePadding = ImVec2(6.0f, 6.0f);
  style.FrameRounding = 0.0f;
  style.FrameBorderSize = 0.0f;
  style.ItemSpacing = ImVec2(12.0f, 6.0f);
  style.ItemInnerSpacing = ImVec2(6.0f, 3.0f);
  style.CellPadding = ImVec2(12.0f, 6.0f);
  style.IndentSpacing = 20.0f;
  style.ColumnsMinSpacing = 6.0f;
  style.ScrollbarSize = 12.0f;
  style.ScrollbarRounding = 0.0f;
  style.GrabMinSize = 12.0f;
  style.GrabRounding = 0.0f;
  style.TabRounding = 0.0f;
  style.TabBorderSize = 0.0f;
  style.TabCloseButtonMinWidthSelected = 0.0f;
  style.TabCloseButtonMinWidthUnselected = 0.0f;
  style.ColorButtonPosition = ImGuiDir_Right;
  style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
  style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

  style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.2745098173618317f, 0.3176470696926117f, 0.4509803950786591f, 1.0f);
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
  style.Colors[ImGuiCol_ChildBg] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
  style.Colors[ImGuiCol_PopupBg] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
  style.Colors[ImGuiCol_Border] = ImVec4(0.1568627506494522f, 0.168627455830574f, 0.1921568661928177f, 1.0f);
  style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
  style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.1568627506494522f, 0.168627455830574f, 0.1921568661928177f, 1.0f);
  style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.2352941185235977f, 0.2156862765550613f, 0.5960784554481506f, 1.0f);
  style.Colors[ImGuiCol_TitleBg] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
  style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
  style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
  style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.09803921729326248f, 0.105882354080677f, 0.1215686276555061f, 1.0f);
  style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
  style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
  style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.1568627506494522f, 0.168627455830574f, 0.1921568661928177f, 1.0f);
  style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
  style.Colors[ImGuiCol_CheckMark] = ImVec4(0.4980392158031464f, 0.5137255191802979f, 1.0f, 1.0f);
  style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.4980392158031464f, 0.5137255191802979f, 1.0f, 1.0f);
  style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.5372549295425415f, 0.5529412031173706f, 1.0f, 1.0f);
  style.Colors[ImGuiCol_Button] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.196078434586525f, 0.1764705926179886f, 0.5450980663299561f, 1.0f);
  style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.2352941185235977f, 0.2156862765550613f, 0.5960784554481506f, 1.0f);
  style.Colors[ImGuiCol_Header] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
  style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.196078434586525f, 0.1764705926179886f, 0.5450980663299561f, 1.0f);
  style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.2352941185235977f, 0.2156862765550613f, 0.5960784554481506f, 1.0f);
  style.Colors[ImGuiCol_Separator] = ImVec4(0.1568627506494522f, 0.1843137294054031f, 0.250980406999588f, 1.0f);
  style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.1568627506494522f, 0.1843137294054031f, 0.250980406999588f, 1.0f);
  style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.1568627506494522f, 0.1843137294054031f, 0.250980406999588f, 1.0f);
  style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
  style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.196078434586525f, 0.1764705926179886f, 0.5450980663299561f, 1.0f);
  style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.2352941185235977f, 0.2156862765550613f, 0.5960784554481506f, 1.0f);
  style.Colors[ImGuiCol_Tab] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
  style.Colors[ImGuiCol_TabHovered] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
  style.Colors[ImGuiCol_TabActive] = ImVec4(0.09803921729326248f, 0.105882354080677f, 0.1215686276555061f, 1.0f);
  style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
  style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
  style.Colors[ImGuiCol_PlotLines] = ImVec4(0.5215686559677124f, 0.6000000238418579f, 0.7019608020782471f, 1.0f);
  style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.03921568766236305f, 0.9803921580314636f, 0.9803921580314636f, 1.0f);
  style.Colors[ImGuiCol_PlotHistogram] = ImVec4(1.0f, 0.2901960909366608f, 0.5960784554481506f, 1.0f);
  style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.9960784316062927f, 0.4745098054409027f, 0.6980392336845398f, 1.0f);
  style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
  style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
  style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
  style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
  style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.09803921729326248f, 0.105882354080677f, 0.1215686276555061f, 1.0f);
  style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.2352941185235977f, 0.2156862765550613f, 0.5960784554481506f, 1.0f);
  style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.4980392158031464f, 0.5137255191802979f, 1.0f, 1.0f);
  style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.4980392158031464f, 0.5137255191802979f, 1.0f, 1.0f);
  style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.4980392158031464f, 0.5137255191802979f, 1.0f, 1.0f);
  style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.196078434586525f, 0.1764705926179886f, 0.5450980663299561f, 0.501960813999176f);
  style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.196078434586525f, 0.1764705926179886f, 0.5450980663299561f, 0.501960813999176f);
}
