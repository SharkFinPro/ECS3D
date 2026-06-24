#include "AssetManager.h"
#include "Asset.h"
#include "ModelAsset.h"
#include "SceneAsset.h"
#include "ScriptAsset.h"
#include "TextureAsset.h"
#include "../objects/Object.h"
#include "../objects/ObjectManager.h"
#include "../scenes/SceneManager.h"
#include <imgui.h>
#include <nfd.h>
#include <nlohmann/json.hpp>
#include <VulkanEngine/components/window/Window.h>
#include <filesystem>
#include <fstream>
#include <ranges>

namespace {
  constexpr ImVec4 kColorSubtle   { 0.55f, 0.55f, 0.55f, 1.00f };
  constexpr ImVec4 kColorAccent   { 0.40f, 0.72f, 1.00f, 1.00f };
  constexpr ImVec4 kColorWarning  { 1.00f, 0.75f, 0.20f, 1.00f };
  constexpr ImVec4 kColorDanger   { 0.95f, 0.35f, 0.35f, 1.00f };
  constexpr ImVec4 kColorSuccess  { 0.35f, 0.85f, 0.55f, 1.00f };

  void pushButtonStyle(const ImVec4 color)
  {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x * 0.7f, color.y * 0.7f, color.z * 0.7f, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x * 0.9f, color.y * 0.9f, color.z * 0.9f, 0.65f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
  }

  void popButtonStyle()
  {
    ImGui::PopStyleColor(3);
  }

  void labeledSeparator(const char* label)
  {
    ImGui::Spacing();
    ImGui::TextColored(kColorSubtle, "%s", label);
    ImGui::Separator();
    ImGui::Spacing();
  }

  std::optional<std::string> openFileDialog(const std::vector<nfdu8filteritem_t>& filters)
  {
    if (NFD_Init() != NFD_OKAY)
    {
      throw std::runtime_error("NFD_Init failed");
    }

    nfdu8char_t* outPath = nullptr;

    const nfdopendialogu8args_t args {
      .filterList = filters.data(),
      .filterCount = static_cast<nfdfiltersize_t>(filters.size())
    };

    std::optional<std::string> result;

    if (NFD_OpenDialogU8_With(&outPath, &args) == NFD_OKAY)
    {
      result = outPath;
      NFD_FreePathU8(outPath);
    }

    NFD_Quit();
    return result;
  }

  bool nameIsValid(const std::string& name)
  {
    if (name.empty())
    {
      return false;
    }

    return std::ranges::none_of(name, [](const char c) {
      return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
    });
  }
}

AssetManager::AssetManager(ECS3D* ecs)
  : m_ecs(ecs)
{}

ECS3D* AssetManager::getECS() const
{
  return m_ecs;
}

void AssetManager::displayMenuWidget()
{
  static PendingAsset pending;
  static bool openPopup = false;
  static std::string errorMessage;

  auto triggerImport = [&](const PendingAsset::Type type,
                           const std::vector<nfdu8filteritem_t>& filters)
  {
    const auto picked = openFileDialog(filters);
    if (!picked)
    {
      return;
    }

    pending.type = type;
    pending.path = *picked;
    pending.name = std::filesystem::path(pending.path).stem().string();
    errorMessage.clear();
    openPopup = true;
  };

  auto triggerCreate = [&](const PendingAsset::Type type,
                           const char* defaultName)
  {
    pending.type = type;
    pending.path.clear();
    pending.name = defaultName;
    errorMessage.clear();
    openPopup = true;
  };

  if (ImGui::BeginMenu("Assets"))
  {
    labeledSeparator("Import");

    if (ImGui::MenuItem("Import Model"))
    {
      triggerImport(PendingAsset::Type::Model, {{ "3D Models", "obj,gltf,glb,fbx" }});
    }

    if (ImGui::MenuItem("Import Texture"))
    {
      triggerImport(PendingAsset::Type::Texture, {{ "Images", "png,jpg,jpeg,tga,bmp" }});
    }

    labeledSeparator("Create");

    if (ImGui::MenuItem("New Scene"))
    {
      triggerCreate(PendingAsset::Type::Scene, "NewScene");
    }

    if (ImGui::MenuItem("New Script"))
    {
      triggerCreate(PendingAsset::Type::Script, "NewScript");
    }

    ImGui::EndMenu();
  }

  if (openPopup)
  {
    ImGui::OpenPopup("###CreateAssetModal");
    openPopup = false;
  }

  displayCreateAssetPopup(pending, errorMessage);
}

void AssetManager::displayCreateAssetPopup(PendingAsset& pending,
                                           std::string& errorMessage)
{
  const std::string title = " Create " + pending.toString() + "###CreateAssetModal";

  ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Always);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(18, 16));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,    ImVec2(8, 10));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,   ImVec2(8, 6));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,  4.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,  6.0f);

  if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::PopStyleVar(5);
    return;
  }

  displayPopupSourcePath(pending);
  displayPopupNameInput(pending, errorMessage);

  if (!errorMessage.empty())
  {
    ImGui::Spacing();
    ImGui::TextColored(kColorDanger, "%s", errorMessage.c_str());
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  displayPopupActionButtons(pending, errorMessage);

  ImGui::EndPopup();
  ImGui::PopStyleVar(5);
}

void AssetManager::displayPopupSourcePath(const PendingAsset& pending)
{
  if (!pending.requiresFilePicker())
  {
    return;
  }

  ImGui::TextColored(kColorSubtle, "Source");

  const std::string display = pending.path.empty()
    ? "(no file selected)"
    : std::filesystem::path(pending.path).filename().string();

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
  ImGui::BeginChild("##sourcepath", ImVec2(-1, 28), false);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

  ImGui::TextColored(pending.path.empty() ? kColorWarning : kColorSuccess, "%s", display.c_str());

  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::Spacing();
}

void AssetManager::displayPopupNameInput(PendingAsset& pending,
                                         std::string& errorMessage)
{
  ImGui::TextColored(kColorSubtle, "Asset Name");

  char nameBuf[128] = {};
  const size_t copyLen = std::min(pending.name.size(), sizeof(nameBuf) - 1);
  std::memcpy(nameBuf, pending.name.data(), copyLen);

  const bool nameInvalid = !nameIsValid(pending.name);

  if (nameInvalid)
  {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.35f, 0.10f, 0.10f, 1.0f));
  }

  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputText("##assetname", nameBuf, sizeof(nameBuf)))
  {
    pending.name = nameBuf;
    errorMessage.clear();
  }

  if (nameInvalid)
  {
    ImGui::PopStyleColor();
  }
}

void AssetManager::displayPopupActionButtons(PendingAsset& pending,
                                             std::string& errorMessage)
{
  const bool canCreate = pending.isValid() && nameIsValid(pending.name) &&
                         (!pending.requiresFilePicker() || !pending.path.empty());

  if (!canCreate)
  {
    ImGui::BeginDisabled();
  }

  pushButtonStyle(kColorAccent);
  if (ImGui::Button("Create", ImVec2(180, 0)))
  {
    try
    {
      commitAssetCreation(pending);
      ImGui::CloseCurrentPopup();
    }
    catch (const std::exception& e)
    {
      errorMessage = e.what();
    }
  }
  popButtonStyle();

  if (!canCreate)
  {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();

  pushButtonStyle(kColorSubtle);
  if (ImGui::Button("Cancel", ImVec2(180, 0)))
  {
    pending = {};
    errorMessage.clear();
    ImGui::CloseCurrentPopup();
  }
  popButtonStyle();
}

void AssetManager::commitAssetCreation(const PendingAsset& pending)
{
  const bool nameExists = std::ranges::any_of(m_assets, [&](const auto& pair) {
    return pair.second->getName() == pending.name;
  });

  if (nameExists)
  {
    throw std::runtime_error("An asset named \"" + pending.name + "\" already exists.");
  }

  switch (pending.type)
  {
    case PendingAsset::Type::Model: commitModelAsset(pending.name, pending.path); break;
    case PendingAsset::Type::Texture: commitTextureAsset(pending.name, pending.path); break;
    case PendingAsset::Type::Scene: commitSceneAsset(pending.name); break;
    case PendingAsset::Type::Script: commitScriptAsset(pending.name); break;
    default: break;
  }

  m_shouldComputeFilteredAssets = true;
}

void AssetManager::commitModelAsset(const std::string& name,
                                    const std::string& srcPath)
{
  const std::filesystem::path dstDir = "assets/models/";
  std::filesystem::create_directories(dstDir);

  const std::filesystem::path dstPath = dstDir / (name + std::filesystem::path(srcPath).extension().string());
  std::filesystem::copy_file(srcPath, dstPath, std::filesystem::copy_options::none);

  const std::string finalPath = dstPath.string();

  auto uuid = m_ecs->createUUID();
  auto asset = std::make_shared<ModelAsset>(uuid, finalPath);
  asset->setManager(this);
  asset->load();

  m_assets.emplace(uuid, asset);
  m_loadedPaths.emplace(finalPath, uuid);
}

void AssetManager::commitTextureAsset(const std::string& name,
                                      const std::string& srcPath)
{
  const std::filesystem::path dstDir = "assets/textures/";
  std::filesystem::create_directories(dstDir);

  const std::filesystem::path dstPath = dstDir / (name + std::filesystem::path(srcPath).extension().string());
  std::filesystem::copy_file(srcPath, dstPath, std::filesystem::copy_options::none);

  const std::string finalPath = dstPath.string();

  auto uuid = m_ecs->createUUID();
  auto asset = std::make_shared<TextureAsset>(uuid, finalPath);
  asset->setManager(this);
  asset->load();

  m_assets.emplace(uuid, asset);
  m_loadedPaths.emplace(finalPath, uuid);
}

void AssetManager::commitSceneAsset(std::string name)
{
  const auto scene = createSceneAsset(std::move(name));

  m_ecs->getSceneManager()->loadScene(scene);
}

void AssetManager::commitScriptAsset(std::string name)
{
  std::filesystem::create_directories("scripts/userScripts/");
  const std::filesystem::path path = "scripts/userScripts/" + name + ".cs";

  std::ofstream out(path);
  out <<
    "using System;\n"
    "using System.Numerics;\n"
    "using ScriptBridge;\n\n"
    "public class " << name << " : ScriptBase\n"
    "{\n"
    "    public override void start() {}\n"
    "    public override void fixedUpdate(float dt) {}\n"
    "    public override void variableUpdate() {}\n"
    "    public override void stop() {}\n"
    "}\n";

  loadScriptAsset(path.string(), std::move(name));
}

void AssetManager::displayGui()
{
  ImGui::Begin("Assets");

  displayAssetsFilterGui();

  computeFilteredAssets();

  constexpr int cellSize = 150;
  const float scaledCellSize = cellSize * m_ecs->getRenderer()->getWindow()->getContentScale();
  const float width = ImGui::GetContentRegionAvail().x;

  ImGui::Columns(std::max(1, static_cast<int>(width / scaledCellSize)), nullptr, false);

  for (const auto& asset : m_filteredAssets)
  {
    ImGui::PushID(&asset);

    asset->displayGui(scaledCellSize * 0.75f);

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
      ImGui::SetDragDropPayload("asset", &asset, sizeof(asset));
      ImGui::Text("%s", asset->getName().c_str());
      ImGui::EndDragDropSource();
    }

    ImGui::PopID();

    ImGui::NextColumn();
  }

  ImGui::End();
}

std::shared_ptr<SceneAsset> AssetManager::createSceneAsset(std::string name)
{
  const uuids::uuid uuid = m_ecs->createUUID();

  const auto asset = std::make_shared<SceneAsset>(uuid, std::move(name));
  asset->setManager(this);
  asset->load();

  m_assets.emplace(uuid, asset);

  m_shouldComputeFilteredAssets = true;

  return asset;
}

void AssetManager::loadScriptAsset(const std::string& path,
                                   std::string className)
{
  if (m_loadedPaths.contains(path))
  {
    return;
  }

  const uuids::uuid uuid = m_ecs->createUUID();

  const auto asset = std::make_shared<ScriptAsset>(uuid, path, std::move(className));
  asset->setManager(this);
  asset->load();

  m_assets.emplace(uuid, asset);

  m_loadedPaths.emplace(path, uuid);

  m_shouldComputeFilteredAssets = true;
}

nlohmann::json AssetManager::serialize()
{
  nlohmann::json data = {
    { "models", nlohmann::json::array() },
    { "textures", nlohmann::json::array() },
    { "scenes", nlohmann::json::array() },
    { "scripts", nlohmann::json::array() }
  };

  for (const auto& [_, asset] : m_assets)
  {
    if (const auto textureAsset = std::dynamic_pointer_cast<TextureAsset>(asset))
    {
      data["textures"].push_back(textureAsset->serialize());
    }
    else if (const auto modelAsset = std::dynamic_pointer_cast<ModelAsset>(asset))
    {
      data["models"].push_back(modelAsset->serialize());
    }
    else if (const auto sceneAsset = std::dynamic_pointer_cast<SceneAsset>(asset))
    {
      data["scenes"].push_back(sceneAsset->serialize());
    }
    else if (const auto scriptAsset = std::dynamic_pointer_cast<ScriptAsset>(asset))
    {
      data["scripts"].push_back(scriptAsset->serialize());
    }
  }

  return data;
}

void AssetManager::loadFromJSON(const nlohmann::json& assetsData)
{
  loadModelsFromJSON(assetsData);

  loadTexturesFromJSON(assetsData);

  loadScenesFromJSON(assetsData);

  loadScriptsFromJSON(assetsData);

  m_shouldComputeFilteredAssets = true;
}

void AssetManager::displayAssetsFilterGui()
{
  if (ImGui::CollapsingHeader("Options"))
  {
    ImGui::Spacing();

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kColorSubtle, "Filter:");
    for (const auto& [type, typeStr] : assetTypeToString)
    {
      bool selected = m_filteredAssetType == type;

      ImGui::SameLine();

      if (ImGui::Checkbox(typeStr.c_str(), &selected))
      {
        m_filteredAssetType = selected ? type : AssetType::Unknown;

        m_shouldComputeFilteredAssets = true;
      }
    }

    char searchBuf[64] = {};
    m_searchQuery.copy(searchBuf, sizeof(searchBuf) - 1);

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kColorSubtle, "Search:");
    ImGui::SameLine();
    if (ImGui::InputText("##Search", searchBuf, sizeof(searchBuf)))
    {
      m_searchQuery = searchBuf;

      m_shouldComputeFilteredAssets = true;
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kColorSubtle, "Sort:");
    ImGui::SameLine();
    if (ImGui::BeginCombo("##SortCombo", sortTypeToString.at(m_sortType).c_str()))
    {
      for (const auto type : { SortType::NameAscending, SortType::NameDescending })
      {
        if (ImGui::Selectable(sortTypeToString.at(type).c_str(), m_sortType == type))
        {
          m_sortType = type;

          m_shouldComputeFilteredAssets = true;
        }
      }

      ImGui::EndCombo();
    }
  }

  ImGui::Separator();
}

void AssetManager::computeFilteredAssets()
{
  if (!m_shouldComputeFilteredAssets)
  {
    return;
  }

  m_filteredAssets.clear();

  for (const auto& [_, asset] : m_assets)
  {
    if (m_filteredAssetType != AssetType::Unknown && asset->getAssetType() != m_filteredAssetType)
    {
      continue;
    }

    if (!m_searchQuery.empty())
    {
      const std::string& assetName = asset->getName();

      const bool match = std::search(assetName.begin(), assetName.end(), m_searchQuery.begin(),
                                     m_searchQuery.end(), [](char a, char b) {
        return std::tolower(a) == std::tolower(b);
      }) != assetName.end();

      if (!match)
      {
        continue;
      }
    }

    m_filteredAssets.emplace_back(asset);
  }

  std::ranges::sort(m_filteredAssets, [this](const auto& a, const auto& b) {
    return std::ranges::lexicographical_compare(a->getName(), b->getName(),
                                                [this](const char x, const char y) {
      return m_sortType == SortType::NameAscending
        ? std::tolower(x) < std::tolower(y)
        : std::tolower(x) > std::tolower(y);
    });
  });

  m_shouldComputeFilteredAssets = false;
}

void AssetManager::loadModelsFromJSON(const nlohmann::json& assetsData)
{
  if (!assetsData.contains("models"))
  {
    return;
  }

  for (const auto& assetData : assetsData.at("models"))
  {
    uuids::uuid uuid = uuids::uuid::from_string(std::string(assetData.at("uuid"))).value();
    const auto& path = assetData.at("filePath");

    const auto asset = std::make_shared<ModelAsset>(uuid, path);
    asset->setManager(this);
    asset->load();

    m_assets.emplace(uuid, asset);

    m_loadedPaths.emplace(path, uuid);
  }
}

void AssetManager::loadTexturesFromJSON(const nlohmann::json& assetsData)
{
  if (!assetsData.contains("textures"))
  {
    return;
  }

  for (const auto& assetData : assetsData.at("textures"))
  {
    uuids::uuid uuid = uuids::uuid::from_string(std::string(assetData.at("uuid"))).value();
    const auto& path = assetData.at("filePath");

    const auto asset = std::make_shared<TextureAsset>(uuid, path);
    asset->setManager(this);
    asset->load();

    m_assets.emplace(uuid, asset);

    m_loadedPaths.emplace(path, uuid);
  }
}

void AssetManager::loadScenesFromJSON(const nlohmann::json& assetsData)
{
  if (!assetsData.contains("scenes"))
  {
    return;
  }

  for (const auto& sceneData : assetsData.at("scenes"))
  {
    uuids::uuid uuid = uuids::uuid::from_string(std::string(sceneData.at("uuid"))).value();
    const auto& name = sceneData.at("name");

    const auto asset = std::make_shared<SceneAsset>(uuid, name);
    asset->setManager(this);
    asset->load();

    auto objectManager = asset->getObjectManager();

    for (const auto& objectData : sceneData.at("objects"))
    {
      auto object = std::make_shared<Object>(objectData, objectManager.get());
      objectManager->addObject(object);

      if (objectData.contains("children"))
      {
        object->loadChildren(objectData.at("children"));
      }
    }

    m_assets.emplace(uuid, asset);
  }
}

void AssetManager::loadScriptsFromJSON(const nlohmann::json& assetsData)
{
  if (!assetsData.contains("scripts"))
  {
    return;
  }

  for (const auto& assetData : assetsData.at("scripts"))
  {
    uuids::uuid uuid = uuids::uuid::from_string(std::string(assetData.at("uuid"))).value();
    const auto& className = assetData.at("className");
    const auto& filePath = assetData.at("filePath");

    const auto asset = std::make_shared<ScriptAsset>(uuid, filePath, className);
    asset->setManager(this);
    asset->load();

    m_assets.emplace(uuid, asset);

    m_loadedPaths.emplace(filePath, uuid);
  }
}
