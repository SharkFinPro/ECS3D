#include "RecordEdits.h"
#include "AssetWireType.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"
#include "scenes/SceneScriptEdits.h"
#include "objects/components/Script.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace edits {

namespace {
  std::optional<uuids::uuid> parseUUIDField(const nlohmann::json& source, const char* field)
  {
    const auto entry = source.find(field);
    if (entry == source.end() || !entry->is_string())
    {
      return std::nullopt;
    }

    return uuids::uuid::from_string(entry->get<std::string>());
  }

  std::string stringField(const nlohmann::json& source, const char* field)
  {
    const auto entry = source.find(field);

    return entry != source.end() && entry->is_string() ? entry->get<std::string>() : std::string{};
  }

  // An op that creates an object only has a faithful undo when it names the uuid the authority will use: the
  // reverse edit targets that uuid. A missing, unparseable or nil uuid leaves nothing to target.
  std::optional<uuids::uuid> createdUUID(const nlohmann::json& edit)
  {
    const auto parsed = parseUUIDField(edit, "uuid");
    if (!parsed.has_value() || parsed->is_nil())
    {
      return std::nullopt;
    }

    return parsed;
  }

  std::optional<uuids::uuid> uuidOf(const std::shared_ptr<Object>& object)
  {
    if (!object)
    {
      return std::nullopt;
    }

    return object->getUUID();
  }

  const std::vector<std::shared_ptr<Object>>& siblingsUnder(const ObjectManager& view,
                                                            const std::shared_ptr<Object>& parent)
  {
    return parent ? parent->getChildren() : view.getObjects();
  }

  // Every creating op appends, so the created object's sibling index is the count that is already there.
  std::size_t appendedIndex(const ObjectManager& view, const std::shared_ptr<Object>& parent)
  {
    return siblingsUnder(view, parent).size();
  }

  std::size_t indexOfSibling(const ObjectManager& view, const std::shared_ptr<Object>& object)
  {
    // Named rather than inlined into the call: siblingsUnder hands back a reference into the parent, so
    // the parent has to outlive it.
    const auto parent = object->getParent();
    const auto& siblings = siblingsUnder(view, parent);

    for (std::size_t index = 0; index < siblings.size(); ++index)
    {
      if (siblings[index] == object)
      {
        return index;
      }
    }

    return siblings.size();
  }

  // The op's optional "parent", resolved the way applySceneEdit resolves it: absent is the scene root,
  // named and unresolvable is a stale view rather than a silent root, so nothing is recorded for it.
  bool resolveParent(const nlohmann::json& edit, const ObjectManager& view,
                     std::shared_ptr<Object>& parent)
  {
    if (!edit.contains("parent"))
    {
      return true;
    }

    const auto parsed = parseUUIDField(edit, "parent");
    if (!parsed.has_value())
    {
      return false;
    }

    parent = view.getObjectByUUID(parsed.value());

    return parent != nullptr;
  }

  std::optional<EditCommand> commandForRemoveComponent(const nlohmann::json& edit,
                                                       const uuids::uuid& objectUUID,
                                                       const std::shared_ptr<Object>& object)
  {
    const std::string type = stringField(edit, "type");
    if (type.empty())
    {
      return std::nullopt;
    }

    // A script is addressed by class name: several coexist under the one component type, so the type
    // alone would not say which of them is going.
    if (edit.contains("className"))
    {
      const std::string className = stringField(edit, "className");

      for (const auto& script : object->getScripts())
      {
        if (auto serialized = script->serialize();
            serialized.value("className", std::string{}) == className)
        {
          return EditCommand::removeComponent(objectUUID, serialized);
        }
      }

      return std::nullopt;
    }

    for (const auto& [componentType, component] : object->getComponents())
    {
      if (auto serialized = component->serialize(); serialized.value("type", std::string{}) == type)
      {
        return EditCommand::removeComponent(objectUUID, serialized);
      }
    }

    return std::nullopt;
  }

  // A fields blob a hand-edited file left as a non-array would make the reverse op malformed, so a
  // recorded blob is always an array.
  nlohmann::json fieldsArrayOf(const nlohmann::json& fields)
  {
    return fields.is_array() ? fields : nlohmann::json::array();
  }

  std::optional<EditCommand> deriveAddSceneScript(const nlohmann::json& edit, const std::string& className,
                                                  const SceneAsset& scene)
  {
    std::size_t landedAt = scene.getScripts().size();
    if (const auto indexField = edit.find("index"); indexField != edit.end())
    {
      const auto index = replication::parseIndexField(*indexField);
      if (!index.has_value())
      {
        return std::nullopt;
      }

      landedAt = std::min(index.value(), landedAt);
    }

    nlohmann::json fields = nullptr;
    if (const auto fieldsField = edit.find("fields"); fieldsField != edit.end())
    {
      if (!replication::isValidSceneScriptFields(*fieldsField))
      {
        return std::nullopt;
      }

      fields = *fieldsField;
    }

    if (scene.findScript(className))
    {
      return std::nullopt;
    }

    return EditCommand::addSceneScript(scene.getUUID(), className, landedAt, fields);
  }

  std::optional<EditCommand> deriveReorderSceneScript(const nlohmann::json& edit, const std::string& className,
                                                      const SceneAsset& scene)
  {
    const auto currentIndex = replication::sceneScriptIndex(scene, className);
    const auto indexField = edit.find("index");
    const auto afterIndex = indexField == edit.end() ? std::nullopt : replication::parseIndexField(*indexField);
    if (!currentIndex.has_value() || !afterIndex.has_value() || afterIndex.value() >= scene.getScripts().size()
        || afterIndex.value() == currentIndex.value())
    {
      return std::nullopt;
    }

    return EditCommand::reorderSceneScript(scene.getUUID(), className, currentIndex.value(), afterIndex.value());
  }

  std::optional<EditCommand> deriveSetSceneScriptFields(const nlohmann::json& edit, const std::string& className,
                                                        const SceneAsset& scene)
  {
    const auto current = scene.findScript(className);
    const auto fieldsField = edit.find("fields");
    if (!current || fieldsField == edit.end() || !replication::isValidSceneScriptFields(*fieldsField))
    {
      return std::nullopt;
    }

    const auto before = fieldsArrayOf(current->getFields());
    if (before == *fieldsField)
    {
      return std::nullopt;
    }

    return EditCommand::setSceneScriptFields(scene.getUUID(), className, before, *fieldsField);
  }

  // The scene script ops, derived from the scene as it stands before the edit. A command is only made for an
  // edit the authority would apply, so a recorded entry never describes a change that did not happen.
  std::optional<EditCommand> commandForSceneScriptEdit(const nlohmann::json& edit, const std::string& op,
                                                       const SceneAsset* scene)
  {
    const auto className = stringField(edit, "className");
    if (!scene || className.empty())
    {
      return std::nullopt;
    }

    if (op == "addSceneScript")
    {
      return deriveAddSceneScript(edit, className, *scene);
    }

    if (op == "reorderSceneScript")
    {
      return deriveReorderSceneScript(edit, className, *scene);
    }

    if (op == "setSceneScriptFields")
    {
      return deriveSetSceneScriptFields(edit, className, *scene);
    }

    const auto current = scene->findScript(className);
    if (!current)
    {
      return std::nullopt;
    }

    auto removed = current->serialize();
    removed["fields"] = fieldsArrayOf(current->getFields());

    return EditCommand::removeSceneScript(scene->getUUID(), replication::sceneScriptIndex(*scene, className).value(),
                                          removed);
  }
}

std::optional<EditCommand> commandForSceneEdit(const nlohmann::json& edit, const ObjectManager& view,
                                               const AssetRegistry* assetRegistry, const SceneAsset* scene)
{
  if (!edit.is_object())
  {
    return std::nullopt;
  }

  const std::string op = stringField(edit, "op");

  if (replication::isSceneScriptEdit(edit))
  {
    return commandForSceneScriptEdit(edit, op, scene);
  }

  if (op == "addObject")
  {
    const auto created = createdUUID(edit);
    if (!created.has_value())
    {
      return std::nullopt;
    }

    std::shared_ptr<Object> parent;
    if (!resolveParent(edit, view, parent))
    {
      return std::nullopt;
    }

    // "Object" is applySceneEdit's own default for an op that names none, so the recorded name matches
    // what the authority will actually create.
    const auto name = edit.find("name");
    std::string createdName = name != edit.end() && name->is_string() ? name->get<std::string>()
                                                                      : std::string{ "Object" };

    return EditCommand::addObject(created.value(), uuidOf(parent), std::move(createdName),
                                  appendedIndex(view, parent));
  }

  if (op == "instantiatePrefab")
  {
    const auto prefabUUID = parseUUIDField(edit, "prefab");
    const auto instanceUUID = createdUUID(edit);
    if (!prefabUUID.has_value() || !instanceUUID.has_value())
    {
      return std::nullopt;
    }

    // Without the registry there is no way to capture the body redo will need to validate against (see
    // EditCommand::instantiatePrefab), so nothing faithful can be recorded - same as any other kind whose
    // pre-edit state the view cannot supply.
    const auto* prefab = assetRegistry ? assetRegistry->getByUUIDOfType(prefabUUID.value(), AssetType::Prefab)
                                       : nullptr;
    if (!prefab)
    {
      return std::nullopt;
    }

    std::shared_ptr<Object> parent;
    if (!resolveParent(edit, view, parent))
    {
      return std::nullopt;
    }

    return EditCommand::instantiatePrefab(prefabUUID.value(), instanceUUID.value(), uuidOf(parent),
                                          appendedIndex(view, parent), prefab->body);
  }

  // Every other op targets an object that has to be in the view for its before state to be readable.
  const auto objectUUID = parseUUIDField(edit, "object");
  if (!objectUUID.has_value())
  {
    return std::nullopt;
  }

  const auto object = view.getObjectByUUID(objectUUID.value());
  if (!object)
  {
    return std::nullopt;
  }

  if (op == "removeObject")
  {
    return EditCommand::removeObject(objectUUID.value(), uuidOf(object->getParent()),
                                     indexOfSibling(view, object), object->serialize());
  }

  if (op == "renameObject")
  {
    const auto name = edit.find("name");
    if (name == edit.end() || !name->is_string())
    {
      return std::nullopt;
    }

    return EditCommand::renameObject(objectUUID.value(), object->getName(), name->get<std::string>());
  }

  if (op == "reparentObject")
  {
    std::optional<uuids::uuid> afterParentUUID;
    if (edit.contains("parent"))
    {
      afterParentUUID = parseUUIDField(edit, "parent");
      if (!afterParentUUID.has_value())
      {
        return std::nullopt;
      }
    }

    return EditCommand::reparentObject(objectUUID.value(), uuidOf(object->getParent()), afterParentUUID);
  }

  if (op == "reorderObject")
  {
    std::optional<uuids::uuid> afterParentUUID;
    if (edit.contains("parent"))
    {
      afterParentUUID = parseUUIDField(edit, "parent");
      if (!afterParentUUID.has_value())
      {
        return std::nullopt;
      }
    }

    const auto indexField = edit.find("index");
    const auto afterIndex = indexField == edit.end() ? std::nullopt : replication::parseIndexField(*indexField);
    if (!afterIndex.has_value())
    {
      return std::nullopt;
    }

    return EditCommand::reorderObject(objectUUID.value(), uuidOf(object->getParent()),
                                      indexOfSibling(view, object), afterParentUUID,
                                      afterIndex.value());
  }

  if (op == "addComponent")
  {
    // buildAddScript sends the same op with the "Script" key plus a class name, which is the identity
    // componentEdit/removeComponent derive for a script too.
    const std::string registryKey = stringField(edit, "component");
    if (registryKey.empty())
    {
      return std::nullopt;
    }

    return EditCommand::addComponent(objectUUID.value(), registryKey, stringField(edit, "className"));
  }

  if (op == "removeComponent")
  {
    return commandForRemoveComponent(edit, objectUUID.value(), object);
  }

  if (op == "duplicateObject")
  {
    const auto duplicateUUID = createdUUID(edit);
    if (!duplicateUUID.has_value())
    {
      return std::nullopt;
    }

    const auto parent = object->getParent();

    return EditCommand::duplicateObject(objectUUID.value(), duplicateUUID.value(), uuidOf(parent),
                                        appendedIndex(view, parent));
  }

  return std::nullopt;
}

std::optional<std::vector<EditCommand>> commandsForSceneEdit(const nlohmann::json& edit, const ObjectManager& view,
                                                              const AssetRegistry* assetRegistry,
                                                              const SceneAsset* scene)
{
  if (!edit.is_object())
  {
    return std::nullopt;
  }

  if (stringField(edit, "op") != "batch")
  {
    auto command = commandForSceneEdit(edit, view, assetRegistry, scene);
    if (!command.has_value())
    {
      return std::nullopt;
    }

    return std::vector<EditCommand>{ std::move(command.value()) };
  }

  const auto opsIt = edit.find("ops");
  if (opsIt == edit.end() || !opsIt->is_array() || opsIt->empty())
  {
    return std::nullopt;
  }

  // The editor never applies a structural edit to its own view before sending it, so view stays pre-batch
  // for every op here - a scratch copy simulates the batch as it derives each op's command, so an op that
  // depends on one before it (deleting a parent and its child together) sees the tree the way the
  // authority will when it applies the batch for real.
  const auto scratch = makeScratchCopy(view);
  std::vector<EditCommand> commands;
  commands.reserve(opsIt->size());

  for (const auto& subEdit : *opsIt)
  {
    auto command = commandForSceneEdit(subEdit, *scratch, assetRegistry);
    if (!command.has_value())
    {
      return std::nullopt;
    }

    commands.push_back(std::move(command.value()));

    if (replication::applySceneEdit(*scratch, subEdit, assetRegistry) != replication::SceneEditResult::applied)
    {
      return std::nullopt;
    }
  }

  return commands;
}

std::optional<EditCommand> commandForAddAsset(const nlohmann::json& asset, const AssetRegistry& view)
{
  if (!asset.is_object())
  {
    return std::nullopt;
  }

  const auto assetUUID = parseUUIDField(asset, "uuid");
  if (!assetUUID.has_value())
  {
    return std::nullopt;
  }

  const AssetType type = assetTypeFromWireString(stringField(asset, "assetType"));
  if (type == AssetType::Unknown)
  {
    return std::nullopt;
  }

  // Prefabs and scenes carry their display name where the file assets carry a path (AssetRecord::path).
  const bool nameKeyed = type == AssetType::Prefab || type == AssetType::Scene;
  const std::string path = nameKeyed ? stringField(asset, "name") : stringField(asset, "path");
  const std::string className = stringField(asset, "className");
  const std::string body = stringField(asset, "body");

  if (const auto* record = view.getByUUID(assetUUID.value()))
  {
    return EditCommand::replaceAsset(assetUUID.value(), type, record->path, record->className,
                                     record->body, path, className, body);
  }

  // registerAsset keys off the path, not the uuid, and the uuid an op carries for a new asset is minted
  // fresh every time (see ObjectGUIManager::saveAsPrefab). So an op whose uuid is unknown but whose path
  // is already registered does NOT create anything under that uuid: a prefab updates the record already
  // under that name and keeps ITS uuid, and every other type is refused outright (first-wins). Recording
  // the incoming uuid either way would put a command on the stack targeting an asset that does not exist.
  if (const auto* byPath = !path.empty() ? view.getByPath(path) : nullptr)
  {
    if (type != AssetType::Prefab || byPath->type != AssetType::Prefab)
    {
      return std::nullopt;
    }

    return EditCommand::replaceAsset(byPath->uuid, type, byPath->path, byPath->className, byPath->body,
                                     path, className, body);
  }

  // A Script op for a class that already has a record merges into it (registerAsset), creating nothing under
  // the incoming uuid, so there is no faithful command to record.
  if (type == AssetType::Script && !className.empty())
  {
    for (const auto& [uuid, record] : view.getAssets())
    {
      if (record.type == AssetType::Script && record.className == className)
      {
        return std::nullopt;
      }
    }
  }

  return EditCommand::addAsset(assetUUID.value(), type, path, className, body);
}

std::optional<EditCommand> commandForRenameAsset(const nlohmann::json& op, const AssetRegistry& view)
{
  if (!op.is_object())
  {
    return std::nullopt;
  }

  const auto assetUUID = parseUUIDField(op, "uuid");
  if (!assetUUID.has_value())
  {
    return std::nullopt;
  }

  const auto* record = view.getByUUID(assetUUID.value());
  if (!record)
  {
    return std::nullopt;
  }

  // A scene's rename target is its real name, which its record holds in `path`.
  return EditCommand::renameAsset(assetUUID.value(),
                                  record->type == AssetType::Scene ? record->path : record->displayName,
                                  stringField(op, "displayName"));
}

std::optional<EditCommand> commandForRemoveAsset(const nlohmann::json& op, const AssetRegistry& view,
                                                 const SceneManager& sceneManager)
{
  if (!op.is_object())
  {
    return std::nullopt;
  }

  const auto assetUUID = parseUUIDField(op, "uuid");
  if (!assetUUID.has_value())
  {
    return std::nullopt;
  }

  const auto* record = view.getByUUID(assetUUID.value());
  if (!record)
  {
    return std::nullopt;
  }

  if (record->type == AssetType::Scene)
  {
    const auto scene = sceneManager.getScene(assetUUID.value());
    if (!scene)
    {
      return std::nullopt;
    }

    return EditCommand::removeAsset(assetUUID.value(), record->type, record->path, record->className,
                                    scene->serialize().dump(), record->displayName);
  }

  return EditCommand::removeAsset(assetUUID.value(), record->type, record->path, record->className,
                                  record->body, record->displayName);
}

}
