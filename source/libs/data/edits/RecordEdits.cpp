#include "RecordEdits.h"
#include "AssetWireType.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include <nlohmann/json.hpp>
#include <cstddef>
#include <memory>
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

  // The ops that create an object do not carry the uuid the authority will mint for it, so a command for
  // one records a nil uuid unless the op happens to name it - read defensively, never required.
  uuids::uuid createdUUID(const nlohmann::json& edit)
  {
    return parseUUIDField(edit, "uuid").value_or(uuids::uuid{});
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
}

std::optional<EditCommand> commandForSceneEdit(const nlohmann::json& edit, const ObjectManager& view,
                                               const AssetRegistry* assetRegistry)
{
  if (!edit.is_object())
  {
    return std::nullopt;
  }

  const std::string op = stringField(edit, "op");

  if (op == "addObject")
  {
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

    return EditCommand::addObject(createdUUID(edit), uuidOf(parent), std::move(createdName),
                                  appendedIndex(view, parent));
  }

  if (op == "instantiatePrefab")
  {
    const auto prefabUUID = parseUUIDField(edit, "prefab");
    if (!prefabUUID.has_value())
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

    return EditCommand::instantiatePrefab(prefabUUID.value(), createdUUID(edit), uuidOf(parent),
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
    if (indexField == edit.end() || !indexField->is_number_unsigned())
    {
      return std::nullopt;
    }

    return EditCommand::reorderObject(objectUUID.value(), uuidOf(object->getParent()),
                                      indexOfSibling(view, object), afterParentUUID,
                                      indexField->get<std::size_t>());
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
    const auto parent = object->getParent();

    return EditCommand::duplicateObject(objectUUID.value(), createdUUID(edit), uuidOf(parent),
                                        appendedIndex(view, parent));
  }

  return std::nullopt;
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

  return EditCommand::renameAsset(assetUUID.value(), record->displayName,
                                  stringField(op, "displayName"));
}

std::optional<EditCommand> commandForRemoveAsset(const nlohmann::json& op, const AssetRegistry& view)
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

  return EditCommand::removeAsset(assetUUID.value(), record->type, record->path, record->className,
                                  record->body);
}

}
