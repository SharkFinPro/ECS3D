#include "EditCommand.h"
#include "Replication.h"
#include "ComponentRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/Script.h"
#include <nlohmann/json.hpp>
#include <Protocol.h>
#include <stdexcept>

namespace edits {

namespace {
  // Same key resolution Object::loadFromJSON uses: a Collider serializes as type "Collider" with a
  // "subType" (Box/Sphere), and is registered under that subType; everything else is registered under
  // its own type name.
  std::string registryKeyFromComponentJSON(const nlohmann::json& serialized)
  {
    const std::string type = serialized.at("type").get<std::string>();
    return type == "Collider" ? serialized.value("subType", std::string{}) : type;
  }

  bool matchesDescriptor(const nlohmann::json& serialized, const std::string& registryKey,
                         const std::string& className)
  {
    const std::string type = serialized.at("type").get<std::string>();

    if (type == "Script")
    {
      return registryKey == "Script" && serialized.value("className", std::string{}) == className;
    }

    return registryKeyFromComponentJSON(serialized) == registryKey;
  }

  // Finds the live component matching a (registryKey, className) descriptor, the same identity a
  // component is addressed by on the wire (Replication.cpp's removeComponent handling matches the same
  // way). Components carry no uuid of their own, so this is the only way to name one.
  std::shared_ptr<Component> findComponent(const std::shared_ptr<Object>& object,
                                           const std::string& registryKey, const std::string& className)
  {
    for (const auto& [type, component] : object->getComponents())
    {
      if (matchesDescriptor(component->serialize(), registryKey, className))
      {
        return component;
      }
    }

    for (const auto& script : object->getScripts())
    {
      if (matchesDescriptor(script->serialize(), registryKey, className))
      {
        return script;
      }
    }

    return nullptr;
  }

  // A fresh, unowned component matching a descriptor - enough to name a slot for buildRemoveComponent, or
  // to loadFromJSON a captured blob into before handing it to buildComponentEdit. Never added to an
  // object; its whole purpose is to be packed.
  std::shared_ptr<Component> buildDescriptorComponent(const ObjectManager& objectManager,
                                                       const std::string& registryKey,
                                                       const std::string& className)
  {
    const auto component = objectManager.getComponentRegistry()->create(registryKey);
    if (!component)
    {
      throw std::runtime_error("EditCommand: registry does not know component type " + registryKey);
    }

    if (const auto script = std::dynamic_pointer_cast<Script>(component))
    {
      script->setClassName(className);
    }

    return component;
  }

  bool parentMatches(const std::shared_ptr<Object>& object, const std::optional<uuids::uuid>& expected)
  {
    const auto parent = object->getParent();

    if (!parent)
    {
      return !expected.has_value();
    }

    return expected.has_value() && parent->getUUID() == expected.value();
  }

  // The asset json shape applyAddAsset/EditorApp's addAsset callback both use. Not in Replication.h
  // because there is no buildAddAsset there - the editor assembles this blob itself (see EditorApp.cpp's
  // addAsset/updatePrefabBody lambdas) and this mirrors that shape exactly.
  std::string assetTypeToWireString(const AssetType type)
  {
    switch (type)
    {
      case AssetType::Model: return "model";
      case AssetType::Texture: return "texture";
      case AssetType::Script: return "script";
      case AssetType::Prefab: return "prefab";
      case AssetType::Scene: return "scene";
      default: return "";
    }
  }

  nlohmann::json buildAddAssetJSON(const uuids::uuid& uuid, const AssetType type, const std::string& path,
                                   const std::string& className, const std::string& body)
  {
    nlohmann::json asset;
    asset["assetType"] = assetTypeToWireString(type);
    asset["uuid"] = uuids::to_string(uuid);

    switch (type)
    {
      case AssetType::Model:
      case AssetType::Texture:
        asset["path"] = path;
        break;
      case AssetType::Script:
        asset["path"] = path;
        asset["className"] = className;
        break;
      case AssetType::Prefab:
        asset["name"] = path;
        asset["body"] = body;
        break;
      case AssetType::Scene:
        asset["name"] = path;
        break;
      default:
        break;
    }

    return asset;
  }
}

EditCommand EditCommand::componentEdit(const uuids::uuid& objectUUID, const nlohmann::json& before,
                                       const nlohmann::json& after)
{
  EditCommand command;
  command.m_kind = CommandKind::componentEdit;
  command.m_data = ComponentEditData{
    .objectUUID = objectUUID,
    .registryKey = registryKeyFromComponentJSON(after),
    .className = after.value("className", std::string{}),
    .beforeJSON = before.dump(),
    .afterJSON = after.dump()
  };
  return command;
}

EditCommand EditCommand::addObject(const uuids::uuid& objectUUID,
                                   const std::optional<uuids::uuid>& parentUUID, std::string name,
                                   const std::size_t siblingIndex)
{
  EditCommand command;
  command.m_kind = CommandKind::addObject;
  command.m_data = AddObjectData{
    .objectUUID = objectUUID,
    .parentUUID = parentUUID,
    .name = std::move(name),
    .siblingIndex = siblingIndex
  };
  return command;
}

EditCommand EditCommand::removeObject(const uuids::uuid& objectUUID,
                                      const std::optional<uuids::uuid>& parentUUID,
                                      const std::size_t siblingIndex, const nlohmann::json& removedSubtree)
{
  EditCommand command;
  command.m_kind = CommandKind::removeObject;
  command.m_data = RemoveObjectData{
    .objectUUID = objectUUID,
    .parentUUID = parentUUID,
    .siblingIndex = siblingIndex,
    .removedSubtreeJSON = removedSubtree.dump()
  };
  return command;
}

EditCommand EditCommand::reparentObject(const uuids::uuid& objectUUID,
                                        const std::optional<uuids::uuid>& beforeParentUUID,
                                        const std::optional<uuids::uuid>& afterParentUUID)
{
  EditCommand command;
  command.m_kind = CommandKind::reparentObject;
  command.m_data = ReparentObjectData{
    .objectUUID = objectUUID,
    .beforeParentUUID = beforeParentUUID,
    .afterParentUUID = afterParentUUID
  };
  return command;
}

EditCommand EditCommand::renameObject(const uuids::uuid& objectUUID, std::string beforeName,
                                      std::string afterName)
{
  EditCommand command;
  command.m_kind = CommandKind::renameObject;
  command.m_data = RenameObjectData{
    .objectUUID = objectUUID,
    .beforeName = std::move(beforeName),
    .afterName = std::move(afterName)
  };
  return command;
}

EditCommand EditCommand::addComponent(const uuids::uuid& objectUUID, std::string registryKey,
                                      std::string className)
{
  EditCommand command;
  command.m_kind = CommandKind::addComponent;
  command.m_data = AddComponentData{
    .objectUUID = objectUUID,
    .registryKey = std::move(registryKey),
    .className = std::move(className)
  };
  return command;
}

EditCommand EditCommand::removeComponent(const uuids::uuid& objectUUID, const nlohmann::json& removedComponent)
{
  EditCommand command;
  command.m_kind = CommandKind::removeComponent;
  command.m_data = RemoveComponentData{
    .objectUUID = objectUUID,
    .registryKey = registryKeyFromComponentJSON(removedComponent),
    .className = removedComponent.value("className", std::string{}),
    .removedComponentJSON = removedComponent.dump()
  };
  return command;
}

EditCommand EditCommand::duplicateObject(const uuids::uuid& sourceUUID, const uuids::uuid& duplicateUUID,
                                         const std::optional<uuids::uuid>& parentUUID,
                                         const std::size_t siblingIndex)
{
  EditCommand command;
  command.m_kind = CommandKind::duplicateObject;
  command.m_data = DuplicateObjectData{
    .sourceUUID = sourceUUID,
    .duplicateUUID = duplicateUUID,
    .parentUUID = parentUUID,
    .siblingIndex = siblingIndex
  };
  return command;
}

EditCommand EditCommand::instantiatePrefab(const uuids::uuid& prefabUUID, const uuids::uuid& instanceUUID,
                                           const std::optional<uuids::uuid>& parentUUID,
                                           const std::size_t siblingIndex)
{
  EditCommand command;
  command.m_kind = CommandKind::instantiatePrefab;
  command.m_data = InstantiatePrefabData{
    .prefabUUID = prefabUUID,
    .instanceUUID = instanceUUID,
    .parentUUID = parentUUID,
    .siblingIndex = siblingIndex
  };
  return command;
}

EditCommand EditCommand::addAsset(const uuids::uuid& assetUUID, const AssetType type, std::string path,
                                  std::string className, std::string body)
{
  EditCommand command;
  command.m_kind = CommandKind::addAsset;
  command.m_data = AddAssetData{
    .assetUUID = assetUUID,
    .type = type,
    .path = std::move(path),
    .className = std::move(className),
    .body = std::move(body)
  };
  return command;
}

EditCommand EditCommand::renameAsset(const uuids::uuid& assetUUID, std::string beforeDisplayName,
                                     std::string afterDisplayName)
{
  EditCommand command;
  command.m_kind = CommandKind::renameAsset;
  command.m_data = RenameAssetData{
    .assetUUID = assetUUID,
    .beforeDisplayName = std::move(beforeDisplayName),
    .afterDisplayName = std::move(afterDisplayName)
  };
  return command;
}

EditCommand EditCommand::removeAsset(const uuids::uuid& assetUUID, const AssetType type, std::string path,
                                     std::string className, std::string body)
{
  EditCommand command;
  command.m_kind = CommandKind::removeAsset;
  command.m_data = RemoveAssetData{
    .assetUUID = assetUUID,
    .type = type,
    .path = std::move(path),
    .className = std::move(className),
    .body = std::move(body)
  };
  return command;
}

CommandKind EditCommand::kind() const
{
  return m_kind;
}

PayloadForm EditCommand::payloadForm() const
{
  switch (m_kind)
  {
    case CommandKind::componentEdit:
    case CommandKind::addAsset:
    case CommandKind::renameAsset:
    case CommandKind::removeAsset:
      return PayloadForm::networkMessage;
    default:
      return PayloadForm::sceneEdit;
  }
}

bool EditCommand::isReversible() const
{
  switch (m_kind)
  {
    case CommandKind::removeObject:
    case CommandKind::removeComponent:
    case CommandKind::duplicateObject:
    case CommandKind::instantiatePrefab:
      return false;
    default:
      return true;
  }
}

uuids::uuid EditCommand::primaryUUID() const
{
  switch (m_kind)
  {
    case CommandKind::componentEdit: return std::get<ComponentEditData>(m_data).objectUUID;
    case CommandKind::addObject: return std::get<AddObjectData>(m_data).objectUUID;
    case CommandKind::removeObject: return std::get<RemoveObjectData>(m_data).objectUUID;
    case CommandKind::reparentObject: return std::get<ReparentObjectData>(m_data).objectUUID;
    case CommandKind::renameObject: return std::get<RenameObjectData>(m_data).objectUUID;
    case CommandKind::addComponent: return std::get<AddComponentData>(m_data).objectUUID;
    case CommandKind::removeComponent: return std::get<RemoveComponentData>(m_data).objectUUID;
    case CommandKind::duplicateObject: return std::get<DuplicateObjectData>(m_data).duplicateUUID;
    case CommandKind::instantiatePrefab: return std::get<InstantiatePrefabData>(m_data).instanceUUID;
    case CommandKind::addAsset: return std::get<AddAssetData>(m_data).assetUUID;
    case CommandKind::renameAsset: return std::get<RenameAssetData>(m_data).assetUUID;
    case CommandKind::removeAsset: return std::get<RemoveAssetData>(m_data).assetUUID;
  }

  throw std::logic_error("EditCommand: unhandled kind");
}

Validation EditCommand::validateForUndo(const ObjectManager& objectManager,
                                        const AssetRegistry* assetRegistry) const
{
  if (!isReversible())
  {
    return { ValidationFailure::notUndoable, primaryUUID() };
  }

  switch (m_kind)
  {
    case CommandKind::componentEdit:
    {
      const auto& data = std::get<ComponentEditData>(m_data);

      const auto object = objectManager.getObjectByUUID(data.objectUUID);
      if (!object)
      {
        return { ValidationFailure::targetMissing, data.objectUUID };
      }

      const auto component = findComponent(object, data.registryKey, data.className);
      if (!component)
      {
        return { ValidationFailure::targetMissing, data.objectUUID };
      }

      if (component->serialize() != nlohmann::json::parse(data.afterJSON))
      {
        return { ValidationFailure::targetChanged, data.objectUUID };
      }

      return {};
    }
    case CommandKind::addObject:
    {
      const auto& data = std::get<AddObjectData>(m_data);

      if (!objectManager.getObjectByUUID(data.objectUUID))
      {
        return { ValidationFailure::targetMissing, data.objectUUID };
      }

      return {};
    }
    case CommandKind::reparentObject:
    {
      const auto& data = std::get<ReparentObjectData>(m_data);

      const auto object = objectManager.getObjectByUUID(data.objectUUID);
      if (!object)
      {
        return { ValidationFailure::targetMissing, data.objectUUID };
      }

      if (!parentMatches(object, data.afterParentUUID))
      {
        return { ValidationFailure::targetChanged, data.objectUUID };
      }

      return {};
    }
    case CommandKind::renameObject:
    {
      const auto& data = std::get<RenameObjectData>(m_data);

      const auto object = objectManager.getObjectByUUID(data.objectUUID);
      if (!object)
      {
        return { ValidationFailure::targetMissing, data.objectUUID };
      }

      if (object->getName() != data.afterName)
      {
        return { ValidationFailure::targetChanged, data.objectUUID };
      }

      return {};
    }
    case CommandKind::addComponent:
    {
      const auto& data = std::get<AddComponentData>(m_data);

      const auto object = objectManager.getObjectByUUID(data.objectUUID);
      if (!object)
      {
        return { ValidationFailure::targetMissing, data.objectUUID };
      }

      if (!findComponent(object, data.registryKey, data.className))
      {
        return { ValidationFailure::targetMissing, data.objectUUID };
      }

      return {};
    }
    case CommandKind::addAsset:
    {
      const auto& data = std::get<AddAssetData>(m_data);

      if (!assetRegistry || !assetRegistry->getByUUID(data.assetUUID))
      {
        return { ValidationFailure::targetMissing, data.assetUUID };
      }

      return {};
    }
    case CommandKind::renameAsset:
    {
      const auto& data = std::get<RenameAssetData>(m_data);

      const auto* record = assetRegistry ? assetRegistry->getByUUID(data.assetUUID) : nullptr;
      if (!record)
      {
        return { ValidationFailure::targetMissing, data.assetUUID };
      }

      if (record->displayName != data.afterDisplayName)
      {
        return { ValidationFailure::targetChanged, data.assetUUID };
      }

      return {};
    }
    case CommandKind::removeAsset:
    {
      const auto& data = std::get<RemoveAssetData>(m_data);

      // "after" a removal is absence; anything currently registered under this uuid is a divergence
      // (someone else re-added it since), not the thing this undo can safely reverse.
      if (assetRegistry && assetRegistry->getByUUID(data.assetUUID))
      {
        return { ValidationFailure::targetChanged, data.assetUUID };
      }

      return {};
    }
    default:
      return { ValidationFailure::notUndoable, primaryUUID() };
  }
}

Validation EditCommand::validateForRedo(const ObjectManager& objectManager,
                                        const AssetRegistry* assetRegistry) const
{
  if (!isReversible())
  {
    return { ValidationFailure::notUndoable, primaryUUID() };
  }

  switch (m_kind)
  {
    case CommandKind::componentEdit:
    {
      const auto& data = std::get<ComponentEditData>(m_data);

      const auto object = objectManager.getObjectByUUID(data.objectUUID);
      if (!object)
      {
        return { ValidationFailure::targetMissing, data.objectUUID };
      }

      const auto component = findComponent(object, data.registryKey, data.className);
      if (!component)
      {
        return { ValidationFailure::targetMissing, data.objectUUID };
      }

      if (component->serialize() != nlohmann::json::parse(data.beforeJSON))
      {
        return { ValidationFailure::targetChanged, data.objectUUID };
      }

      return {};
    }
    case CommandKind::addObject:
    {
      const auto& data = std::get<AddObjectData>(m_data);

      // "before" an addition is that its parent (if any) is there to add under; the object itself is not
      // expected to exist yet (redo mints a fresh uuid on the object - see buildRedoJSON).
      if (data.parentUUID && !objectManager.getObjectByUUID(*data.parentUUID))
      {
        return { ValidationFailure::targetMissing, *data.parentUUID };
      }

      return {};
    }
    case CommandKind::reparentObject:
    {
      const auto& data = std::get<ReparentObjectData>(m_data);

      const auto object = objectManager.getObjectByUUID(data.objectUUID);
      if (!object)
      {
        return { ValidationFailure::targetMissing, data.objectUUID };
      }

      if (!parentMatches(object, data.beforeParentUUID))
      {
        return { ValidationFailure::targetChanged, data.objectUUID };
      }

      return {};
    }
    case CommandKind::renameObject:
    {
      const auto& data = std::get<RenameObjectData>(m_data);

      const auto object = objectManager.getObjectByUUID(data.objectUUID);
      if (!object)
      {
        return { ValidationFailure::targetMissing, data.objectUUID };
      }

      if (object->getName() != data.beforeName)
      {
        return { ValidationFailure::targetChanged, data.objectUUID };
      }

      return {};
    }
    case CommandKind::addComponent:
    {
      const auto& data = std::get<AddComponentData>(m_data);

      const auto object = objectManager.getObjectByUUID(data.objectUUID);
      if (!object)
      {
        return { ValidationFailure::targetMissing, data.objectUUID };
      }

      if (findComponent(object, data.registryKey, data.className))
      {
        return { ValidationFailure::targetChanged, data.objectUUID };
      }

      return {};
    }
    case CommandKind::addAsset:
    {
      const auto& data = std::get<AddAssetData>(m_data);

      if (assetRegistry && assetRegistry->getByUUID(data.assetUUID))
      {
        return { ValidationFailure::targetChanged, data.assetUUID };
      }

      return {};
    }
    case CommandKind::renameAsset:
    {
      const auto& data = std::get<RenameAssetData>(m_data);

      const auto* record = assetRegistry ? assetRegistry->getByUUID(data.assetUUID) : nullptr;
      if (!record)
      {
        return { ValidationFailure::targetMissing, data.assetUUID };
      }

      if (record->displayName != data.beforeDisplayName)
      {
        return { ValidationFailure::targetChanged, data.assetUUID };
      }

      return {};
    }
    case CommandKind::removeAsset:
    {
      const auto& data = std::get<RemoveAssetData>(m_data);

      const auto* record = assetRegistry ? assetRegistry->getByUUID(data.assetUUID) : nullptr;
      if (!record)
      {
        return { ValidationFailure::targetMissing, data.assetUUID };
      }

      if (record->type != data.type || record->path != data.path || record->className != data.className
          || record->body != data.body)
      {
        return { ValidationFailure::targetChanged, data.assetUUID };
      }

      return {};
    }
    default:
      return { ValidationFailure::notUndoable, primaryUUID() };
  }
}

nlohmann::json EditCommand::buildUndoJSON(const ObjectManager& objectManager) const
{
  switch (m_kind)
  {
    case CommandKind::addObject:
    {
      const auto& data = std::get<AddObjectData>(m_data);
      return replication::buildRemoveObject(data.objectUUID);
    }
    case CommandKind::reparentObject:
    {
      const auto& data = std::get<ReparentObjectData>(m_data);
      return data.beforeParentUUID
        ? replication::buildReparentObject(data.objectUUID, &*data.beforeParentUUID)
        : replication::buildReparentObject(data.objectUUID);
    }
    case CommandKind::renameObject:
    {
      const auto& data = std::get<RenameObjectData>(m_data);
      return replication::buildRenameObject(data.objectUUID, data.beforeName);
    }
    case CommandKind::addComponent:
    {
      const auto& data = std::get<AddComponentData>(m_data);
      const auto descriptor = buildDescriptorComponent(objectManager, data.registryKey, data.className);
      return replication::buildRemoveComponent(data.objectUUID, descriptor);
    }
    default:
      throw std::logic_error("EditCommand::buildUndoJSON: not a sceneEdit-form command");
  }
}

nlohmann::json EditCommand::buildRedoJSON(const ObjectManager& objectManager) const
{
  (void)objectManager; // every sceneEdit-form redo below builds straight from stored fields

  switch (m_kind)
  {
    case CommandKind::addObject:
    {
      const auto& data = std::get<AddObjectData>(m_data);
      return data.parentUUID
        ? replication::buildAddObject(data.name, &*data.parentUUID)
        : replication::buildAddObject(data.name);
    }
    case CommandKind::reparentObject:
    {
      const auto& data = std::get<ReparentObjectData>(m_data);
      return data.afterParentUUID
        ? replication::buildReparentObject(data.objectUUID, &*data.afterParentUUID)
        : replication::buildReparentObject(data.objectUUID);
    }
    case CommandKind::renameObject:
    {
      const auto& data = std::get<RenameObjectData>(m_data);
      return replication::buildRenameObject(data.objectUUID, data.afterName);
    }
    case CommandKind::addComponent:
    {
      const auto& data = std::get<AddComponentData>(m_data);
      return data.registryKey == "Script"
        ? replication::buildAddScript(data.objectUUID, data.className)
        : replication::buildAddComponent(data.objectUUID, data.registryKey);
    }
    default:
      throw std::logic_error("EditCommand::buildRedoJSON: not a sceneEdit-form command");
  }
}

net::Message EditCommand::buildUndoMessage(const ObjectManager& objectManager) const
{
  switch (m_kind)
  {
    case CommandKind::componentEdit:
    {
      const auto& data = std::get<ComponentEditData>(m_data);
      const auto component = buildDescriptorComponent(objectManager, data.registryKey, data.className);
      component->loadFromJSON(nlohmann::json::parse(data.beforeJSON));
      return replication::buildComponentEdit(data.objectUUID, component);
    }
    case CommandKind::addAsset:
    {
      const auto& data = std::get<AddAssetData>(m_data);
      return replication::packRemoveAsset(replication::buildRemoveAsset(data.assetUUID));
    }
    case CommandKind::renameAsset:
    {
      const auto& data = std::get<RenameAssetData>(m_data);
      return replication::packRenameAsset(
        replication::buildRenameAsset(data.assetUUID, data.beforeDisplayName));
    }
    case CommandKind::removeAsset:
    {
      const auto& data = std::get<RemoveAssetData>(m_data);
      return replication::packAddAsset(
        buildAddAssetJSON(data.assetUUID, data.type, data.path, data.className, data.body));
    }
    default:
      throw std::logic_error("EditCommand::buildUndoMessage: not a networkMessage-form command");
  }
}

net::Message EditCommand::buildRedoMessage(const ObjectManager& objectManager) const
{
  switch (m_kind)
  {
    case CommandKind::componentEdit:
    {
      const auto& data = std::get<ComponentEditData>(m_data);
      const auto component = buildDescriptorComponent(objectManager, data.registryKey, data.className);
      component->loadFromJSON(nlohmann::json::parse(data.afterJSON));
      return replication::buildComponentEdit(data.objectUUID, component);
    }
    case CommandKind::addAsset:
    {
      const auto& data = std::get<AddAssetData>(m_data);
      return replication::packAddAsset(
        buildAddAssetJSON(data.assetUUID, data.type, data.path, data.className, data.body));
    }
    case CommandKind::renameAsset:
    {
      const auto& data = std::get<RenameAssetData>(m_data);
      return replication::packRenameAsset(
        replication::buildRenameAsset(data.assetUUID, data.afterDisplayName));
    }
    case CommandKind::removeAsset:
    {
      const auto& data = std::get<RemoveAssetData>(m_data);
      return replication::packRemoveAsset(replication::buildRemoveAsset(data.assetUUID));
    }
    default:
      throw std::logic_error("EditCommand::buildRedoMessage: not a networkMessage-form command");
  }
}

}
