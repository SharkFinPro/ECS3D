#include "AssetBrowserPanel.h"
#include "AssetDragDrop.h"
#include "GuiComponents.h"
#include "Selection.h"
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

namespace {
  constexpr std::array<std::pair<AssetType, const char*>, 5> kAssetTypeLabels {{
    { AssetType::Model,   "Model"   },
    { AssetType::Texture, "Texture" },
    { AssetType::Scene,   "Scene"   },
    { AssetType::Script,  "Script"  },
    { AssetType::Prefab,  "Prefab"  },
  }};

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

void AssetBrowserPanel::setSelection(std::shared_ptr<EditorSelection> selection)
{
  m_selection = std::move(selection);
}

void AssetBrowserPanel::setEditable(const bool editable)
{
  m_editable = editable;
}

void AssetBrowserPanel::recomputeCache()
{
  const std::string query = [this] {
    std::string q = m_search;
    std::ranges::transform(q, q.begin(), [](const unsigned char c) { return std::tolower(c); });
    return q;
  }();

  m_cachedAssets.clear();

  for (const auto& [uuid, record] : m_assetRegistry->getAssets())
  {
    if (m_filter != AssetType::Unknown && record.type != m_filter)
    {
      continue;
    }

    if (!query.empty())
    {
      std::string lowered = displayName(record);
      std::ranges::transform(lowered, lowered.begin(), [](const unsigned char c) { return std::tolower(c); });

      if (lowered.find(query) == std::string::npos)
      {
        continue;
      }
    }

    m_cachedAssets.emplace_back(uuid, record);
  }

  const auto ciNameLess = [](const std::string& l, const std::string& r) {
    return std::ranges::lexicographical_compare(l, r, [](const char x, const char y) {
      return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
  };

  std::ranges::sort(m_cachedAssets, [this, &ciNameLess](const auto& a, const auto& b) {
    const std::string nameA = displayName(a.second);
    const std::string nameB = displayName(b.second);

    switch (m_sortType)
    {
      case SortType::NameDescending:
        return ciNameLess(nameB, nameA);
      case SortType::Type:
      {
        // Group by type label, then by name within each type.
        const std::string typeA = assetTypeLabel(a.second.type);
        const std::string typeB = assetTypeLabel(b.second.type);
        return typeA != typeB ? ciNameLess(typeA, typeB) : ciNameLess(nameA, nameB);
      }
      case SortType::NameAscending:
      default:
        return ciNameLess(nameA, nameB);
    }
  });

  m_lastRegistryVersion = m_assetRegistry->getVersion();
  m_dirty = false;
}

const char* AssetBrowserPanel::assetTypeLabel(const AssetType type)
{
  switch (type)
  {
    case AssetType::Model: return "Model";
    case AssetType::Texture: return "Texture";
    case AssetType::Scene: return "Scene";
    case AssetType::Script: return "Script";
    case AssetType::Prefab: return "Prefab";
    default: return "All";
  }
}

std::string AssetBrowserPanel::displayName(const AssetRecord& record)
{
  switch (record.type)
  {
    // Scenes and prefabs store their display name in path (neither is a file).
    case AssetType::Scene:
    case AssetType::Prefab: return record.path;
    case AssetType::Script: return record.className;
    default: return std::filesystem::path(record.path).filename().string();
  }
}

void AssetBrowserPanel::displayGui()
{
  ImGui::Begin("Assets");

  if (ImGui::CollapsingHeader("Options", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::Spacing();

    // Filter chips on one row (label sits inline with its chips).
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::t2, "Filter");
    for (const auto& [type, label] : kAssetTypeLabels)
    {
      bool selected = m_filter == type;
      ImGui::SameLine(0.0f, 16.0f);
      if (gc::accentCheckboxCompact(label, &selected))
      {
        m_filter = selected ? type : AssetType::Unknown;
        m_dirty = true;
      }
    }

    // Search + Sort share a row (mockup): the search box fills the row, the sort combo sits on the right.
    constexpr float sortWidth = 200.0f;
    constexpr float gap = 10.0f;
    const float searchWidth = std::max(120.0f, ImGui::GetContentRegionAvail().x - sortWidth - gap);

    if (gc::searchField("##Search", m_search, sizeof(m_search), "Search assets", searchWidth))
    {
      m_dirty = true;
    }

    ImGui::SameLine(0.0f, gap);
    ImGui::SetNextItemWidth(sortWidth);
    const char* sortLabel = m_sortType == SortType::NameAscending  ? "Name (A-Z)"
                          : m_sortType == SortType::NameDescending ? "Name (Z-A)"
                                                                   : "Type";
    if (ImGui::BeginCombo("##Sort", sortLabel))
    {
      if (ImGui::Selectable("Name (A-Z)", m_sortType == SortType::NameAscending))
      {
        m_sortType = SortType::NameAscending;
        m_dirty = true;
      }
      if (ImGui::Selectable("Name (Z-A)", m_sortType == SortType::NameDescending))
      {
        m_sortType = SortType::NameDescending;
        m_dirty = true;
      }
      if (ImGui::Selectable("Type", m_sortType == SortType::Type))
      {
        m_sortType = SortType::Type;
        m_dirty = true;
      }
      ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::Spacing();
  }

  ImGui::Separator();

  // Dirty from registry changes or filter/sort/search changes.
  if (m_assetRegistry->getVersion() != m_lastRegistryVersion)
  {
    m_dirty = true;
  }

  if (m_dirty)
  {
    recomputeCache();
  }

  // Grid: manual wrapping (auto-fill columns with a min tile width, stretched to fill) with even
  // gutters both directions, matching the mockup's CSS grid. Tiles are placed at absolute positions so
  // the card->label spacing stays independent of the inter-tile gap.
  const float contentScale = m_assetCache->getRenderer()->getWindow()->getContentScale();
  const float gap = 14.0f * contentScale;
  const float minTile = 132.0f * contentScale;
  const float available = ImGui::GetContentRegionAvail().x;

  const int columns = std::max(1, static_cast<int>((available + gap) / (minTile + gap)));
  const float tileWidth = (available - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
  const float cellHeight = tileWidth + ImGui::GetStyle().ItemSpacing.y + ImGui::GetTextLineHeight();

  const ImVec2 origin = ImGui::GetCursorScreenPos();

  int index = 0;
  for (const auto& [uuid, record] : m_cachedAssets)
  {
    const int row = index / columns;
    const int col = index % columns;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + static_cast<float>(col) * (tileWidth + gap),
                                     origin.y + static_cast<float>(row) * (cellHeight + gap)));

    ImGui::PushID(uuids::to_string(uuid).c_str());
    ImGui::BeginGroup();
    displayAsset(uuid, record, tileWidth, displayName(record));
    ImGui::EndGroup();
    ImGui::PopID();

    ++index;
  }

  // Reserve the full grid extent so scrolling + the window content region are correct.
  if (index > 0)
  {
    const int rows = (index + columns - 1) / columns;
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(available, static_cast<float>(rows) * cellHeight + static_cast<float>(rows - 1) * gap));
  }

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

void AssetBrowserPanel::displayAsset(const uuids::uuid& uuid, const AssetRecord& record, const float cellSize, const std::string& name) const
{
  // Per-type icon + accent color for the card (textures instead show their actual image).
  ImTextureID thumb = 0;
  gc::SecIcon icon = gc::SecIcon::none;
  ImVec4 iconCol = theme::accent;

  switch (record.type)
  {
    case AssetType::Texture:
      iconCol = theme::accent;
      try
      {
        if (const auto texture = m_assetCache->getTexture(uuid))
        {
          thumb = texture->getImGuiTexture();
        }
      }
      catch (const std::exception&)
      {
        // fall back to the type icon below
      }
      icon = gc::SecIcon::image;
      break;
    case AssetType::Model:  icon = gc::SecIcon::model;  iconCol = theme::modelPurple; break;
    case AssetType::Script: icon = gc::SecIcon::script; iconCol = theme::scriptAmber; break;
    case AssetType::Scene:  icon = gc::SecIcon::scene;  iconCol = theme::sceneGreen;  break;
    case AssetType::Prefab: icon = gc::SecIcon::block;  iconCol = theme::prefabBlue;  break;
    default: break;
  }

  // Selecting an asset is a view action (not a mutation), so it stays available on a read-only server.
  const bool selected = m_selection && m_selection->assetUUID() == uuid;
  if (gc::assetCard(cellSize, thumb, icon, iconCol, assetTypeLabel(record.type), iconCol, selected) && m_selection)
  {
    // Writes the shared selection slot: this deselects any object (and clears any other asset), since
    // there is a single selection at a time.
    m_selection->selectAsset(uuid);
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

  gc::assetCardLabel(name.c_str(), cellSize);
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

  auto labeledSeparator = [](const char* label)
  {
    ImGui::Spacing();
    ImGui::TextColored(theme::t3, "%s", label);
    ImGui::Separator();
    ImGui::Spacing();
  };

  if (ImGui::BeginMenu("Assets"))
  {
    labeledSeparator("Import");

    // Creating assets adds them to the authoritative project, so it's disabled on a read-only server.
    ImGui::BeginDisabled(!m_editable);

    if (ImGui::MenuItem("Import Model"))
    {
      if (const auto picked = pickFile({ { "3D Models", "glb,gltf,obj,fbx" } }))
      {
        beginCreate(PendingAsset::Type::Model, *picked, std::filesystem::path(*picked).stem().string().c_str());
      }
    }

    if (ImGui::MenuItem("Import Texture"))
    {
      if (const auto picked = pickFile({ { "Images", "png,jpg,jpeg,tga,bmp" } }))
      {
        beginCreate(PendingAsset::Type::Texture, *picked, std::filesystem::path(*picked).stem().string().c_str());
      }
    }

    labeledSeparator("Create");

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

  // Reserve a consistent width so the auto-resized popup doesn't jump between sources/names.
  ImGui::Dummy(ImVec2(320.0f, 0.0f));

  if (!m_pending.sourcePath.empty())
  {
    gc::rowLabel("Source");
    ImGui::TextColored(theme::t1, "%s", std::filesystem::path(m_pending.sourcePath).filename().string().c_str());
  }

  gc::rowLabel("Name");
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  ImGui::InputText("##name", m_pending.name, sizeof(m_pending.name));

  if (!m_createError.empty())
  {
    ImGui::TextColored(theme::danger, "%s", m_createError.c_str());
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Accent confirm; neutral cancel (mirrors the Scene Status Start button + delete modal).
  ImGui::PushStyleColor(ImGuiCol_Button, theme::accent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::v4(60, 200, 224));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::v4(60, 200, 224));
  ImGui::PushStyleColor(ImGuiCol_Text, theme::onAcc);
  if (ImGui::Button("Create", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter))
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
  ImGui::PopStyleColor(4);

  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
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
