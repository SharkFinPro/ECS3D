#include "ECS3D.h"
#include "SaveManager.h"
#include "scenes/SceneManager.h"
#include "assets/AssetManager.h"
#include <VulkanEngine/components/imGui/ImGuiInstance.h>
#include <VulkanEngine/components/window/Window.h>

ECS3D::ECS3D()
  : m_previousTime(std::chrono::steady_clock::now()),
		m_sceneManager(std::make_shared<SceneManager>(this)),
		m_assetManager(std::make_shared<AssetManager>(this)),
		m_rng([] {
			std::random_device rd;
			auto seed_data = std::array<int, std::mt19937::state_size>{};
			std::ranges::generate(seed_data, std::ref(rd));
			std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
			return std::mt19937(seq);
		}()),
		m_uuidGenerator(m_rng)
{
  initRenderer();

	m_saveManager = std::make_shared<SaveManager>(this);
}

void ECS3D::prepareForReset()
{
	m_previousSceneManager = m_sceneManager;
	m_previousAssetManager = m_assetManager;

	m_sceneManager.reset();
	m_assetManager.reset();

	m_sceneManager = std::make_shared<SceneManager>(this);
	m_assetManager = std::make_shared<AssetManager>(this);
}

void ECS3D::cancelReset()
{
	m_sceneManager.reset();
	m_assetManager.reset();

	m_sceneManager = m_previousSceneManager;
	m_assetManager = m_previousAssetManager;

	m_previousSceneManager.reset();
	m_previousAssetManager.reset();
}

void ECS3D::completeReset()
{
	m_previousSceneManager.reset();
	m_previousAssetManager.reset();
}

bool ECS3D::isActive() const
{
  return m_renderer->isActive();
}

void ECS3D::update()
{
	fixedUpdate();

	variableUpdate();
}

void ECS3D::fixedUpdate()
{
	const auto currentTime = std::chrono::steady_clock::now();
	const float dt = std::chrono::duration<float>(currentTime - m_previousTime).count();
	m_previousTime = currentTime;

	m_timeAccumulator += dt;

	uint8_t steps = 1;
	while (m_timeAccumulator >= m_fixedUpdateDt && steps <= 3)
	{
		++steps;

		try {
			m_sceneManager->fixedUpdate(m_fixedUpdateDt);
		}
		catch(const std::exception& e)
		{
			logMessage("Error", e.what());
		}

		m_timeAccumulator -= m_fixedUpdateDt;
	}
}

void ECS3D::variableUpdate()
{
	updateDockSpace();

	displayMenuBar();

	try {
		m_assetManager->displayGui();

		m_sceneManager->variableUpdate();
	}
	catch(const std::exception& e)
	{
		logMessage("Error", e.what());
	}

	displayMessageLog();

	m_renderer->render();
}

std::shared_ptr<vke::VulkanEngine> ECS3D::getRenderer() const
{
  return m_renderer;
}

bool ECS3D::keyIsPressed(const int key) const
{
  return m_renderer->getWindow()->keyIsPressed(key);
}

std::shared_ptr<SceneManager> ECS3D::getSceneManager() const
{
  return m_sceneManager;
}

std::shared_ptr<AssetManager> ECS3D::getAssetManager() const
{
	return m_assetManager;
}

std::shared_ptr<SaveManager> ECS3D::getSaveManager() const
{
	return m_saveManager;
}

void ECS3D::logMessage(const std::string& level, const std::string& message)
{
	m_errorMessages.push_back("[" + level + "] " + message);
}

uuids::uuid ECS3D::createUUID()
{
	return m_uuidGenerator();
}

void ECS3D::displayMessageLog()
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

void ECS3D::initRenderer()
{
	const vke::EngineConfig engineConfig {
		.window {
			.width = 1280,
			.height = 720,
			.title = "ECS3D"
		},
		.camera {
			.position = { 0, 5, -50 }
		},
		.imGui {
			.maxTextures = 100
		}
	};

  m_renderer = std::make_shared<vke::VulkanEngine>(engineConfig);

  ImGui::SetCurrentContext(vke::ImGuiInstance::getImGuiContext());
	setupImGuiStyle();

	m_sceneViewName = engineConfig.imGui.sceneViewName;
}

void ECS3D::displayMenuBar() const
{
	if (ImGui::BeginMainMenuBar())
	{
		m_renderer->getImGuiInstance()->setMenuBarHeight(ImGui::GetWindowSize().y);

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New"))
			{
				m_saveManager->createNewProject();
			}

			if (ImGui::MenuItem("Open"))
			{
				m_saveManager->loadSaveFile();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Save", "Ctrl+S"))
			{
				m_saveManager->save();
			}

			if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
			{
				m_saveManager->saveAs();
			}

			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}
}

void ECS3D::updateDockSpace() const
{
	const auto gui = m_renderer->getImGuiInstance();

	gui->setTopDockPercent(0.09);
	gui->setBottomDockPercent(0.28);

	gui->setLeftDockPercent(0.2);
	gui->setRightDockPercent(0.35);

	gui->dockCenter(m_sceneViewName.c_str());

	gui->dockLeft("Objects");
	gui->dockLeft("Scenes");

	gui->dockRight("Selected Object");

	gui->dockTop("Scene Status");

	gui->dockBottom("Assets");
	gui->dockBottom("Project Errors");
	gui->dockBottom("Smoke");
	gui->dockBottom("Elliptical Dots");
}

void ECS3D::setupImGuiStyle()
{
	// Future Dark style by rewrking from ImThemes
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
