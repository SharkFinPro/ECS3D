#include "ModelRendererBindings.h"
#include "BindingContext.h"
#include <assets/AssetRegistry.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/ModelRenderer.h>
#include <memory>
#include <string>
#include <utility>

namespace {
  // Returned strings point into this buffer, valid until the next ModelRendererBindings call on the
  // same thread. Scripts run synchronously on the server loop thread, so the managed caller marshals the
  // result to a C# string immediately (see World.cs / ModelRenderer.cs) before the next call overwrites it.
  thread_local std::string s_returnBuffer;

  const char* store(std::string value)
  {
    s_returnBuffer = std::move(value);
    return s_returnBuffer.c_str();
  }

  // Also hands back the parsed object uuid, so a setter that needs to record a replicated edit doesn't
  // have to re-parse the string it just resolved.
  std::shared_ptr<ModelRenderer> find(const char* uuid, uuids::uuid* outObjectUUID = nullptr)
  {
    const auto objectManager = BindingContext::getObjectManager();
    if (!objectManager || !uuid)
    {
      return nullptr;
    }

    const auto parsed = uuids::uuid::from_string(std::string(uuid));
    if (!parsed.has_value())
    {
      return nullptr;
    }

    const auto object = objectManager->getObjectByUUID(parsed.value());
    if (!object)
    {
      return nullptr;
    }

    const auto modelRenderer = object->getComponent<ModelRenderer>(ComponentType::modelRenderer);
    if (modelRenderer && outObjectUUID)
    {
      *outObjectUUID = parsed.value();
    }

    return modelRenderer;
  }

  // Validate an asset uuid string against the AssetRegistry: it must parse, be registered, and be of the
  // expected type. An unknown or wrong-typed asset fails safely rather than being assigned.
  bool resolveAsset(const char* assetUUID, const AssetType expected, uuids::uuid& out)
  {
    const auto assetRegistry = BindingContext::getAssetRegistry();
    if (!assetRegistry || !assetUUID)
    {
      return false;
    }

    const auto parsed = uuids::uuid::from_string(std::string(assetUUID));
    if (!parsed.has_value())
    {
      return false;
    }

    if (!assetRegistry->getByUUIDOfType(parsed.value(), expected))
    {
      return false;
    }

    out = parsed.value();
    return true;
  }
}

ModelRendererBindings ModelRendererBindingsProvider::getBindings()
{
  return ModelRendererBindings {
    .getModelUUID = &bindGetModelUUID,
    .getTextureUUID = &bindGetTextureUUID,
    .getShouldRender = &bindGetShouldRender,
    .setModelUUID = &bindSetModelUUID,
    .setTextureUUID = &bindSetTextureUUID,
    .setShouldRender = &bindSetShouldRender,
    .has = &bindHas
  };
}

const char* ModelRendererBindingsProvider::bindGetModelUUID(const char* uuid)
{
  const auto modelRenderer = find(uuid);
  if (!modelRenderer)
  {
    return store("");
  }

  const auto modelUUID = modelRenderer->getModelUUID();
  return store(modelUUID.is_nil() ? "" : uuids::to_string(modelUUID));
}

const char* ModelRendererBindingsProvider::bindGetTextureUUID(const char* uuid)
{
  const auto modelRenderer = find(uuid);
  if (!modelRenderer)
  {
    return store("");
  }

  const auto textureUUID = modelRenderer->getTextureUUID();
  return store(textureUUID.is_nil() ? "" : uuids::to_string(textureUUID));
}

bool ModelRendererBindingsProvider::bindGetShouldRender(const char* uuid)
{
  const auto modelRenderer = find(uuid);
  if (!modelRenderer)
  {
    return false;
  }

  return modelRenderer->getShouldRender();
}

bool ModelRendererBindingsProvider::bindSetModelUUID(const char* uuid, const char* modelUUID)
{
  uuids::uuid objectUUID;
  const auto modelRenderer = find(uuid, &objectUUID);
  if (!modelRenderer)
  {
    return false;
  }

  uuids::uuid parsedAsset;
  if (!resolveAsset(modelUUID, AssetType::Model, parsedAsset))
  {
    return false;
  }

  modelRenderer->setModelUUID(parsedAsset);

  // Not covered by the per-tick state delta (Transform only), so replicate it like the editor's own
  // component edits: buffer it here (scripting can't reach the net layer) for the app to broadcast.
  BindingContext::recordComponentEdit(objectUUID, modelRenderer);

  return true;
}

bool ModelRendererBindingsProvider::bindSetTextureUUID(const char* uuid, const char* textureUUID)
{
  uuids::uuid objectUUID;
  const auto modelRenderer = find(uuid, &objectUUID);
  if (!modelRenderer)
  {
    return false;
  }

  uuids::uuid parsedAsset;
  if (!resolveAsset(textureUUID, AssetType::Texture, parsedAsset))
  {
    return false;
  }

  modelRenderer->setTextureUUID(parsedAsset);

  BindingContext::recordComponentEdit(objectUUID, modelRenderer);

  return true;
}

void ModelRendererBindingsProvider::bindSetShouldRender(const char* uuid, const bool shouldRender)
{
  uuids::uuid objectUUID;
  const auto modelRenderer = find(uuid, &objectUUID);
  if (!modelRenderer)
  {
    return;
  }

  modelRenderer->setShouldRender(shouldRender);

  BindingContext::recordComponentEdit(objectUUID, modelRenderer);
}

bool ModelRendererBindingsProvider::bindHas(const char* uuid)
{
  return find(uuid) != nullptr;
}
