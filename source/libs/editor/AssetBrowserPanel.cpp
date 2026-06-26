#include "AssetBrowserPanel.h"
#include "AssetDragDrop.h"
#include <GpuAssetCache.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/window/Window.h>
#include <VulkanEngine/components/assets/textures/Texture2D.h>
#include <nfd.h>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace {
  // Pick a single existing file through the OS dialog. filters = { { label, "ext1,ext2" }, ... }.
  std::optional<std::string> pickFile(const std::vector<nfdu8filteritem_t>& filters)
  {
    if (NFD_Init() != NFD_OKAY)
    {
      return std::nullopt;
    }

    nfdu8char_t* outPath = nullptr;
    const nfdopendialogu8args_t args {
      .filterList = filters.data(),
      .filterCount = static_cast<nfdfiltersize_t>(filters.size())
    };

    std::optional<std::string> result;
    if (NFD_OpenDialogU8_With(&outPath, &args) == NFD_OKAY)
    {
      result = std::string(outPath);
      NFD_FreePathU8(outPath);
    }

    NFD_Quit();
    return result;
  }

  [[nodiscard]] std::string newUUID()
  {
    std::mt19937 rng{ std::random_device{}() };
    uuids::uuid_random_generator generator{ rng };
    return uuids::to_string(generator());
  }

  [[nodiscard]] bool nameIsValid(const std::string& name)
  {
    return !name.empty() && std::ranges::none_of(name, [](const char c) {
      return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
    });
  }
}

AssetBrowserPanel::AssetBrowserPanel(const AssetRegistry* assetRegistry, std::shared_ptr<GpuAssetCache> assetCache)
  : m_assetRegistry(assetRegistry),
    m_assetCache(std::move(assetCache))
{}

void AssetBrowserPanel::setLoadSceneCallback(LoadSceneCallback callback)
{
  m_onLoadScene = std::move(callback);
}

void AssetBrowserPanel::setAddAssetCallback(AddAssetCallback callback)
{
  m_onAddAsset = std::move(callback);
}

void AssetBrowserPanel::setEditable(const bool editable)
{
  m_editable = editable;
}

const char* AssetBrowserPanel::assetTypeLabel(const AssetType type)
{
  switch (type)
  {
    case AssetType::Model: return "Model";
    case AssetType::Texture: return "Texture";
    case AssetType::Scene: return "Scene";
    case AssetType::Script: return "Script";
    default: return "All";
  }
}

std::string AssetBrowserPanel::displayName(const AssetRecord& record)
{
  switch (record.type)
  {
    case AssetType::Scene: return record.path;        // scenes store their display name in path
    case AssetType::Script: return record.className;
    default: return std::filesystem::path(record.path).filename().string();
  }
}

void AssetBrowserPanel::displayGui()
{
  ImGui::Begin("Assets");

  // Type filter.
  if (ImGui::BeginCombo("Type", assetTypeLabel(m_filter)))
  {
    for (const auto type : { AssetType::Unknown, AssetType::Model, AssetType::Texture, AssetType::Scene, AssetType::Script })
    {
      if (ImGui::Selectable(assetTypeLabel(type), m_filter == type))
      {
        m_filter = type;
      }
    }

    ImGui::EndCombo();
  }

  ImGui::InputText("Search", m_search, sizeof(m_search));

  ImGui::Separator();

  std::string query = m_search;
  std::ranges::transform(query, query.begin(), [](const unsigned char c) { return std::tolower(c); });

  // Grid: size the tiles by the window content scale and pack as many columns as fit.
  const float contentScale = m_assetCache->getRenderer()->getWindow()->getContentScale();
  const float cellSize = 72.0f * contentScale;
  const float available = ImGui::GetContentRegionAvail().x;
  const int columns = std::max(1, static_cast<int>(available / (cellSize + 16.0f)));

  ImGui::Columns(columns, nullptr, false);

  for (const auto& [uuid, record] : m_assetRegistry->getAssets())
  {
    if (m_filter != AssetType::Unknown && record.type != m_filter)
    {
      continue;
    }

    const std::string name = displayName(record);

    if (!query.empty())
    {
      std::string lowered = name;
      std::ranges::transform(lowered, lowered.begin(), [](const unsigned char c) { return std::tolower(c); });

      if (lowered.find(query) == std::string::npos)
      {
        continue;
      }
    }

    ImGui::PushID(uuids::to_string(uuid).c_str());

    displayAsset(uuid, record, cellSize, name);

    ImGui::PopID();

    ImGui::NextColumn();
  }

  ImGui::Columns(1);

  ImGui::End();

  // Open + draw the create-asset modal at the top level (the menu only flags it). Opening it from
  // inside the main menu bar is unreliable.
  if (m_openCreatePopup)
  {
    ImGui::OpenPopup("Create Asset");
    m_openCreatePopup = false;
  }

  displayCreateAssetPopup();
}

void AssetBrowserPanel::displayAsset(const uuids::uuid& uuid, const AssetRecord& record, const float cellSize, const std::string& name)
{
  ImGui::TextWrapped("%s", name.c_str());

  bool drewThumbnail = false;

  // Textures show their actual image; everything else gets a labelled tile.
  if (record.type == AssetType::Texture)
  {
    try
    {
      if (const auto texture = m_assetCache->getTexture(uuid))
      {
        ImGui::ImageButton("##thumb", texture->getImGuiTexture(), { cellSize, cellSize });
        drewThumbnail = true;
      }
    }
    catch (const std::exception&)
    {
      // fall back to a labelled tile below
    }
  }

  if (!drewThumbnail)
  {
    ImGui::Button(assetTypeLabel(record.type), { cellSize, cellSize });
  }

  // Double-click a scene tile to make it the active scene. Switching the active scene is a server-side
  // mutation, so it's only available when the server is editable (a read-only viewer follows the
  // server's current scene).
  if (m_editable && record.type == AssetType::Scene && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
  {
    if (m_onLoadScene)
    {
      m_onLoadScene(uuid);
    }
  }

  // Drag source (model/texture/script), tagged by type so only the right drop target accepts it. Only
  // when editable: assigning an asset to a component is a mutation, and ImGui::BeginDisabled does not
  // block drag-drop targets, so suppressing the source here is what actually makes those slots read-only.
  if (const char* payloadId = m_editable ? assetDragDrop::payloadId(record.type) : nullptr)
  {
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
      const std::string uuidStr = uuids::to_string(uuid);
      ImGui::SetDragDropPayload(payloadId, uuidStr.c_str(), uuidStr.size());
      ImGui::TextUnformatted(name.c_str());
      ImGui::EndDragDropSource();
    }
  }
}

void AssetBrowserPanel::displayMenuWidget()
{
  const auto beginCreate = [&](const PendingAsset::Type type, const std::string& source, const char* defaultName) {
    m_pending = {};
    m_pending.type = type;
    m_pending.sourcePath = source;
    std::strncpy(m_pending.name, defaultName, sizeof(m_pending.name) - 1);
    m_createError.clear();
    m_openCreatePopup = true;
  };

  if (ImGui::BeginMenu("Assets"))
  {
    // Creating assets adds them to the authoritative project, so it's disabled on a read-only server.
    ImGui::BeginDisabled(!m_editable);

    if (ImGui::MenuItem("Import Model..."))
    {
      if (const auto picked = pickFile({ { "3D Models", "glb,gltf,obj,fbx" } }))
      {
        beginCreate(PendingAsset::Type::Model, *picked, std::filesystem::path(*picked).stem().string().c_str());
      }
    }

    if (ImGui::MenuItem("Import Texture..."))
    {
      if (const auto picked = pickFile({ { "Images", "png,jpg,jpeg,tga,bmp" } }))
      {
        beginCreate(PendingAsset::Type::Texture, *picked, std::filesystem::path(*picked).stem().string().c_str());
      }
    }

    ImGui::Separator();

    if (ImGui::MenuItem("New Scene"))
    {
      beginCreate(PendingAsset::Type::Scene, "", "New Scene");
    }

    if (ImGui::MenuItem("New Script"))
    {
      beginCreate(PendingAsset::Type::Script, "", "NewScript");
    }

    ImGui::EndDisabled();

    ImGui::EndMenu();
  }

  // The popup itself is opened/drawn from displayGui at the top level (not here, inside the menu bar),
  // where ImGui popups behave reliably — displayMenuWidget only records the request.
}

void AssetBrowserPanel::displayCreateAssetPopup()
{
  if (!ImGui::BeginPopupModal("Create Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    return;
  }

  if (!m_pending.sourcePath.empty())
  {
    ImGui::Text("Source: %s", std::filesystem::path(m_pending.sourcePath).filename().string().c_str());
  }

  ImGui::SetNextItemWidth(280.0f);
  ImGui::InputText("Name", m_pending.name, sizeof(m_pending.name));

  if (!m_createError.empty())
  {
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", m_createError.c_str());
  }

  ImGui::Separator();

  if (ImGui::Button("Create", ImVec2(120, 0)))
  {
    if (!nameIsValid(m_pending.name))
    {
      m_createError = "Enter a valid name (no path characters).";
    }
    else
    {
      try
      {
        commitAsset();
        ImGui::CloseCurrentPopup();
      }
      catch (const std::exception& e)
      {
        m_createError = e.what();
      }
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120, 0)))
  {
    m_pending = {};
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}

void AssetBrowserPanel::commitAsset()
{
  if (!m_onAddAsset)
  {
    return;
  }

  const std::string name = m_pending.name;
  const std::string uuid = newUUID();

  // Copy an imported file into the project's assets dir (relative to the working dir = exe dir) so the
  // GpuAssetCache can load it; then send the registry record to the authoritative server.
  const auto importFile = [&](const char* dir) {
    const std::filesystem::path destDir = dir;
    std::filesystem::create_directories(destDir);

    const auto dest = destDir / (name + std::filesystem::path(m_pending.sourcePath).extension().string());
    std::filesystem::copy_file(m_pending.sourcePath, dest, std::filesystem::copy_options::overwrite_existing);

    return dest.generic_string();
  };

  nlohmann::json addAsset;

  switch (m_pending.type)
  {
    case PendingAsset::Type::Model:
      addAsset = { { "assetType", "model" }, { "uuid", uuid }, { "path", importFile("assets/models") } };
      break;
    case PendingAsset::Type::Texture:
      addAsset = { { "assetType", "texture" }, { "uuid", uuid }, { "path", importFile("assets/textures") } };
      break;
    case PendingAsset::Type::Scene:
      addAsset = { { "assetType", "scene" }, { "uuid", uuid }, { "name", name } };
      break;
    case PendingAsset::Type::Script:
    {
      std::filesystem::create_directories("scripts/UserScripts");
      const std::filesystem::path path = std::filesystem::path("scripts/UserScripts") / (name + ".cs");

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

      addAsset = { { "assetType", "script" }, { "uuid", uuid }, { "path", path.generic_string() }, { "className", name } };
      break;
    }
    default:
      return;
  }

  m_onAddAsset(addAsset);
  m_pending = {};
}
