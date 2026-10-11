#include "SceneScriptEdits.h"
#include "SceneAsset.h"
#include "objects/components/Script.h"
#include <nlohmann/json.hpp>
#include <memory>

namespace replication {

namespace {
  constexpr auto addOp = "addSceneScript";
  constexpr auto removeOp = "removeSceneScript";
  constexpr auto reorderOp = "reorderSceneScript";
  constexpr auto setFieldsOp = "setSceneScriptFields";

  // The op's non-empty string className, nullopt when missing, empty or not a string.
  std::optional<std::string> classNameOf(const nlohmann::json& edit)
  {
    const auto entry = edit.find("className");
    if (entry == edit.end() || !entry->is_string() || entry->get<std::string>().empty())
    {
      return std::nullopt;
    }

    return entry->get<std::string>();
  }

  SceneEditResult applyAdd(SceneAsset& scene, const nlohmann::json& edit, const std::string& className)
  {
    std::optional<std::size_t> index;
    if (const auto indexField = edit.find("index"); indexField != edit.end())
    {
      index = parseIndexField(*indexField);
      if (!index.has_value())
      {
        return SceneEditResult::malformedEdit;
      }
    }

    const auto fieldsField = edit.find("fields");
    if (fieldsField != edit.end() && !isValidSceneScriptFields(*fieldsField))
    {
      return SceneEditResult::malformedEdit;
    }

    if (scene.findScript(className))
    {
      return SceneEditResult::rejected;
    }

    const auto script = std::make_shared<Script>(className);
    if (fieldsField != edit.end())
    {
      script->setFields(*fieldsField);
    }

    return scene.addScript(script, index) ? SceneEditResult::applied : SceneEditResult::rejected;
  }

  SceneEditResult applyReorder(SceneAsset& scene, const nlohmann::json& edit, const std::string& className)
  {
    const auto indexField = edit.find("index");
    const auto index = indexField == edit.end() ? std::nullopt : parseIndexField(*indexField);
    if (!index.has_value())
    {
      return SceneEditResult::malformedEdit;
    }

    if (!scene.findScript(className))
    {
      return SceneEditResult::unknownComponent;
    }

    return scene.moveScript(className, index.value()) ? SceneEditResult::applied : SceneEditResult::rejected;
  }

  SceneEditResult applySetFields(SceneAsset& scene, const nlohmann::json& edit, const std::string& className)
  {
    const auto fieldsField = edit.find("fields");
    if (fieldsField == edit.end() || !isValidSceneScriptFields(*fieldsField))
    {
      return SceneEditResult::malformedEdit;
    }

    const auto script = scene.findScript(className);
    if (!script)
    {
      return SceneEditResult::unknownComponent;
    }

    script->setFields(*fieldsField);

    return SceneEditResult::applied;
  }
}

nlohmann::json buildAddSceneScript(const std::string& className, const std::optional<std::size_t> index,
                                   const nlohmann::json* fields)
{
  nlohmann::json edit = {
    { "op", addOp },
    { "className", className }
  };

  if (index.has_value())
  {
    edit["index"] = index.value();
  }

  if (fields)
  {
    edit["fields"] = *fields;
  }

  return edit;
}

nlohmann::json buildRemoveSceneScript(const std::string& className)
{
  return {
    { "op", removeOp },
    { "className", className }
  };
}

nlohmann::json buildReorderSceneScript(const std::string& className, const std::size_t index)
{
  return {
    { "op", reorderOp },
    { "className", className },
    { "index", index }
  };
}

nlohmann::json buildSetSceneScriptFields(const std::string& className, const nlohmann::json& fields)
{
  return {
    { "op", setFieldsOp },
    { "className", className },
    { "fields", fields }
  };
}

bool isSceneScriptEdit(const nlohmann::json& edit)
{
  if (!edit.is_object())
  {
    return false;
  }

  const auto op = edit.find("op");
  if (op == edit.end() || !op->is_string())
  {
    return false;
  }

  const auto name = op->get<std::string>();

  return name == addOp || name == removeOp || name == reorderOp || name == setFieldsOp;
}

SceneEditResult applySceneScriptEdit(SceneAsset& scene, const nlohmann::json& edit)
{
  if (!isSceneScriptEdit(edit))
  {
    return SceneEditResult::malformedEdit;
  }

  const auto className = classNameOf(edit);
  if (!className.has_value())
  {
    return SceneEditResult::malformedEdit;
  }

  const auto op = edit.at("op").get<std::string>();

  if (op == addOp)
  {
    return applyAdd(scene, edit, className.value());
  }

  if (op == removeOp)
  {
    return scene.removeScript(className.value()) ? SceneEditResult::applied : SceneEditResult::unknownComponent;
  }

  if (op == reorderOp)
  {
    return applyReorder(scene, edit, className.value());
  }

  return applySetFields(scene, edit, className.value());
}

bool isValidSceneScriptFields(const nlohmann::json& fields)
{
  if (!fields.is_array())
  {
    return false;
  }

  for (const auto& field : fields)
  {
    if (!field.is_object() || !field.contains("name") || !field.at("name").is_string()
        || !field.contains("type") || !field.at("type").is_string())
    {
      return false;
    }
  }

  return true;
}

std::optional<std::size_t> sceneScriptIndex(const SceneAsset& scene, const std::string& className)
{
  const auto& scripts = scene.getScripts();

  for (std::size_t index = 0; index < scripts.size(); ++index)
  {
    if (scripts[index]->getClassName() == className)
    {
      return index;
    }
  }

  return std::nullopt;
}

}
