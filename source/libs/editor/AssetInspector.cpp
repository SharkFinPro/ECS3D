#include "AssetInspector.h"
#include "AssetDisplay.h"
#include "GuiComponents.h"
#include <GpuAssetCache.h>
#include <assets/AssetRegistry.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/window/Window.h>
#include <VulkanEngine/components/assets/textures/Texture2D.h>
#include <VulkanEngine/components/assets/objects/Model.h>
#include <stb_image.h>
#include <imgui.h>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

AssetInspector::AssetInspector(std::shared_ptr<GpuAssetCache> assetCache)
  : m_assetCache(std::move(assetCache))
{}

void AssetInspector::displayTypeChip(const AssetRecord& record) const
{
  const char* label = assetDisplay::typeLabel(record.type);
  ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - gc::iconPillWidth(label));
  gc::iconPill(assetDisplay::icon(record.type), label, assetDisplay::color(record.type));
}

void AssetInspector::display(const AssetRecord& record)
{
  // Recompute the file metadata only when the selection changes, not every frame.
  if (!m_metaLoaded || m_metaUUID != record.uuid)
  {
    m_metaUUID = record.uuid;
    m_metaLoaded = true;
    refreshMeta(record);
  }

  displayHeader(record);

  switch (record.type)
  {
    case AssetType::Texture: displayTextureBody(record); break;
    case AssetType::Model:   displayModelBody(record);   break;
    default: break;
  }
}

void AssetInspector::displayHeader(const AssetRecord& record) const
{
  // Display name (primary text), then metadata rows shared by every asset type.
  ImGui::TextColored(theme::t1, "%s", assetDisplay::name(record).c_str());

  ImGui::Spacing();

  gc::rowLabel("UUID");
  ImGui::TextColored(theme::t3, "%s", uuids::to_string(record.uuid).c_str());

  // Scenes and prefabs have no file on disk (their `path` holds the display name), so only the file-backed
  // types show a source path.
  if (record.type != AssetType::Scene && record.type != AssetType::Prefab && !record.path.empty())
  {
    gc::rowLabel("Path");
    ImGui::TextColored(theme::t2, "%s", record.path.c_str());
  }
}

void AssetInspector::refreshMeta(const AssetRecord& record)
{
  m_fileSize.clear();
  m_haveImageSize = false;
  m_imageWidth = 0;
  m_imageHeight = 0;
  m_haveMeshStats = false;
  m_vertexCount = 0;
  m_indexCount = 0;

  // Only file-backed types have a path on disk; scenes/prefabs carry their name there instead.
  if (record.type == AssetType::Scene || record.type == AssetType::Prefab || record.path.empty())
  {
    return;
  }

  if (const auto size = assetDisplay::fileSize(record.path))
  {
    m_fileSize = *size;
  }

  // The GPU texture doesn't expose its extent, so read the dimensions straight from the file header
  // (stbi_info decodes no pixels). Degrades gracefully when the file isn't reachable editor-side.
  if (record.type == AssetType::Texture)
  {
    int comp = 0;
    m_haveImageSize = stbi_info(record.path.c_str(), &m_imageWidth, &m_imageHeight, &comp) != 0;
  }

  // Mesh stats come from the loaded vke::Model (its vertex/index arrays are already in memory once the
  // model is cached). Resolving it here — once per selection — reuses the same cache the renderer uses.
  if (record.type == AssetType::Model)
  {
    try
    {
      if (const auto model = m_assetCache->getModel(record.uuid))
      {
        m_vertexCount = model->getVertices().size();
        m_indexCount = model->getIndices().size();
        m_haveMeshStats = true;
      }
    }
    catch (const std::exception&)
    {
      // The model file may be unreachable editor-side; leave the stats unshown.
    }
  }
}

void AssetInspector::displayTextureBody(const AssetRecord& record)
{
  ImGui::Spacing();
  gc::sectionLabel("Preview");
  ImGui::Spacing();

  // Same fallback-to-icon behavior as the browser tiles: use the cached GPU texture when it resolves,
  // otherwise fall through to the type icon.
  ImTextureID thumb = 0;
  try
  {
    if (const auto texture = m_assetCache->getTexture(record.uuid))
    {
      thumb = texture->getImGuiTexture();
    }
  }
  catch (const std::exception&)
  {
    // fall back to the type icon below
  }

  // Fit the preview inside the panel width and a scaled max height, preserving the image aspect ratio
  // when the dimensions are known (square otherwise).
  const float scale = m_assetCache->getRenderer()->getWindow()->getContentScale();
  const float maxW = ImGui::GetContentRegionAvail().x;
  const float maxH = 260.0f * scale;

  float boxW = maxW;
  float boxH = maxW;
  if (m_haveImageSize && m_imageWidth > 0 && m_imageHeight > 0)
  {
    const float aspect = static_cast<float>(m_imageWidth) / static_cast<float>(m_imageHeight);
    boxH = boxW / aspect;
    if (boxH > maxH)
    {
      boxH = maxH;
      boxW = boxH * aspect;
    }
  }

  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 p1(p0.x + boxW, p0.y + boxH);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p0, p1, theme::u32(theme::inset), 8.0f);
  gc::drawThumb(dl, p0, p1, thumb, assetDisplay::icon(record.type), assetDisplay::color(record.type), 8.0f);
  dl->AddRect(p0, p1, theme::u32(theme::line), 8.0f);
  ImGui::Dummy(ImVec2(boxW, boxH));

  ImGui::Spacing();

  if (m_haveImageSize)
  {
    gc::rowLabel("Dimensions");
    ImGui::TextColored(theme::t2, "%d x %d", m_imageWidth, m_imageHeight);
  }

  if (!m_fileSize.empty())
  {
    gc::rowLabel("Size");
    ImGui::TextColored(theme::t2, "%s", m_fileSize.c_str());
  }
}

void AssetInspector::displayModelBody(const AssetRecord& record)
{
  ImGui::Spacing();
  gc::sectionLabel("Preview");
  ImGui::Spacing();

  // A live 3D preview needs an offscreen render target the engine doesn't expose yet (deferred, B3), so
  // stand in the type icon.
  const float scale = m_assetCache->getRenderer()->getWindow()->getContentScale();
  const float boxW = ImGui::GetContentRegionAvail().x;
  const float boxH = std::min(boxW, 200.0f * scale);
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 p1(p0.x + boxW, p0.y + boxH);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p0, p1, theme::u32(theme::inset), 8.0f);
  gc::drawThumb(dl, p0, p1, 0, assetDisplay::icon(record.type), assetDisplay::color(record.type), 8.0f);
  dl->AddRect(p0, p1, theme::u32(theme::line), 8.0f);
  ImGui::Dummy(ImVec2(boxW, boxH));

  ImGui::Spacing();

  std::string ext = std::filesystem::path(record.path).extension().string();
  if (!ext.empty() && ext.front() == '.')
  {
    ext.erase(0, 1); // drop the leading dot for the "Format" row
  }
  gc::rowLabel("Format");
  ImGui::TextColored(theme::t2, "%s", ext.empty() ? "-" : ext.c_str());

  if (!m_fileSize.empty())
  {
    gc::rowLabel("Size");
    ImGui::TextColored(theme::t2, "%s", m_fileSize.c_str());
  }

  if (m_haveMeshStats)
  {
    gc::rowLabel("Vertices");
    ImGui::TextColored(theme::t2, "%zu", m_vertexCount);

    gc::rowLabel("Triangles");
    ImGui::TextColored(theme::t2, "%zu", m_indexCount / 3);
  }
}
