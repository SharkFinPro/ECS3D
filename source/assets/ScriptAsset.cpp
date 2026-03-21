#include "ScriptAsset.h"
#include "AssetManager.h"
#include "../objects/components/Script.h"
#include <imgui.h>

ScriptAsset::ScriptAsset(const uuids::uuid uuid,
                         std::string path,
                         std::string className)
  : Asset(AssetType::Script, uuid, className), m_path(std::move(path)), m_className(std::move(className))
{}

void ScriptAsset::load()
{
  m_scriptManager = m_assetManager->getECS()->getScriptManager();
}

void ScriptAsset::displayGui(const float cellSize)
{
  Asset::displayGui(cellSize);

  ImGui::Button("Script", { cellSize, cellSize });
}

std::shared_ptr<Script> ScriptAsset::createScript() const
{
  return std::make_shared<Script>(m_className, m_scriptManager);
}

nlohmann::json ScriptAsset::serialize()
{
  nlohmann::json data = {
    { "className", m_className },
    { "filePath", m_path },
    { "uuid", uuids::to_string(m_uuid) }
  };

  return data;
}
