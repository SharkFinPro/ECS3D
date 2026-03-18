#include "ScriptAsset.h"
#include "../objects/components/Script.h"
#include <imgui.h>

ScriptAsset::ScriptAsset(const uuids::uuid uuid,
                         std::string path,
                         std::string className,
                         std::shared_ptr<ScriptManager> scriptManager)
  : Asset(uuid, std::move(className)), m_scriptManager(std::move(scriptManager)), m_path(std::move(path)),
    m_className(std::move(className))
{}

void ScriptAsset::displayGui()
{
  Asset::displayGui();

  ImGui::Button("Script", {150, 150});
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
