#include "LightRendererBindings.h"
#include "BindingContext.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/LightRenderer.h>
#include <memory>
#include <string>

namespace {
  // Also hands back the parsed object uuid, so a setter that needs to record a replicated edit doesn't
  // have to re-parse the string it just resolved.
  std::shared_ptr<LightRenderer> find(const char* uuid, uuids::uuid* outObjectUUID = nullptr)
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

    const auto lightRenderer = object->getComponent<LightRenderer>(ComponentType::lightRenderer);
    if (lightRenderer && outObjectUUID)
    {
      *outObjectUUID = parsed.value();
    }

    return lightRenderer;
  }
}

LightRendererBindings LightRendererBindingsProvider::getBindings()
{
  return LightRendererBindings {
    .getIsSpotLight = &bindGetIsSpotLight,
    .getColor = &bindGetColor,
    .getAmbient = &bindGetAmbient,
    .getDiffuse = &bindGetDiffuse,
    .getSpecular = &bindGetSpecular,
    .getDirection = &bindGetDirection,
    .getConeAngle = &bindGetConeAngle,
    .setSpotLight = &bindSetSpotLight,
    .setColor = &bindSetColor,
    .setAmbient = &bindSetAmbient,
    .setDiffuse = &bindSetDiffuse,
    .setSpecular = &bindSetSpecular,
    .setDirection = &bindSetDirection,
    .setConeAngle = &bindSetConeAngle,
    .has = &bindHas
  };
}

bool LightRendererBindingsProvider::bindGetIsSpotLight(const char* uuid)
{
  const auto lightRenderer = find(uuid);
  if (!lightRenderer)
  {
    return false;
  }

  return lightRenderer->isSpotLight();
}

void LightRendererBindingsProvider::bindGetColor(const char* uuid, float* r, float* g, float* b)
{
  const auto lightRenderer = find(uuid);
  if (!lightRenderer)
  {
    return;
  }

  const auto color = lightRenderer->getColor();
  *r = color.x;
  *g = color.y;
  *b = color.z;
}

float LightRendererBindingsProvider::bindGetAmbient(const char* uuid)
{
  const auto lightRenderer = find(uuid);
  if (!lightRenderer)
  {
    return 0.0f;
  }

  return lightRenderer->getAmbient();
}

float LightRendererBindingsProvider::bindGetDiffuse(const char* uuid)
{
  const auto lightRenderer = find(uuid);
  if (!lightRenderer)
  {
    return 0.0f;
  }

  return lightRenderer->getDiffuse();
}

float LightRendererBindingsProvider::bindGetSpecular(const char* uuid)
{
  const auto lightRenderer = find(uuid);
  if (!lightRenderer)
  {
    return 0.0f;
  }

  return lightRenderer->getSpecular();
}

void LightRendererBindingsProvider::bindGetDirection(const char* uuid, float* x, float* y, float* z)
{
  const auto lightRenderer = find(uuid);
  if (!lightRenderer)
  {
    return;
  }

  const auto direction = lightRenderer->getDirection();
  *x = direction.x;
  *y = direction.y;
  *z = direction.z;
}

float LightRendererBindingsProvider::bindGetConeAngle(const char* uuid)
{
  const auto lightRenderer = find(uuid);
  if (!lightRenderer)
  {
    return 0.0f;
  }

  return lightRenderer->getConeAngle();
}

void LightRendererBindingsProvider::bindSetSpotLight(const char* uuid, const bool isSpotLight)
{
  uuids::uuid objectUUID;
  const auto lightRenderer = find(uuid, &objectUUID);
  if (!lightRenderer)
  {
    return;
  }

  lightRenderer->setSpotLight(isSpotLight);

  // Not covered by the per-tick state delta (Transform only), so replicate it like the editor's own
  // component edits: buffer it here (scripting can't reach the net layer) for the app to broadcast.
  BindingContext::recordComponentEdit(objectUUID, lightRenderer);
}

void LightRendererBindingsProvider::bindSetColor(const char* uuid, const float r, const float g, const float b)
{
  uuids::uuid objectUUID;
  const auto lightRenderer = find(uuid, &objectUUID);
  if (!lightRenderer)
  {
    return;
  }

  lightRenderer->setColor({ r, g, b });

  BindingContext::recordComponentEdit(objectUUID, lightRenderer);
}

void LightRendererBindingsProvider::bindSetAmbient(const char* uuid, const float ambient)
{
  uuids::uuid objectUUID;
  const auto lightRenderer = find(uuid, &objectUUID);
  if (!lightRenderer)
  {
    return;
  }

  lightRenderer->setAmbient(ambient);

  BindingContext::recordComponentEdit(objectUUID, lightRenderer);
}

void LightRendererBindingsProvider::bindSetDiffuse(const char* uuid, const float diffuse)
{
  uuids::uuid objectUUID;
  const auto lightRenderer = find(uuid, &objectUUID);
  if (!lightRenderer)
  {
    return;
  }

  lightRenderer->setDiffuse(diffuse);

  BindingContext::recordComponentEdit(objectUUID, lightRenderer);
}

void LightRendererBindingsProvider::bindSetSpecular(const char* uuid, const float specular)
{
  uuids::uuid objectUUID;
  const auto lightRenderer = find(uuid, &objectUUID);
  if (!lightRenderer)
  {
    return;
  }

  lightRenderer->setSpecular(specular);

  BindingContext::recordComponentEdit(objectUUID, lightRenderer);
}

void LightRendererBindingsProvider::bindSetDirection(const char* uuid, const float x, const float y, const float z)
{
  uuids::uuid objectUUID;
  const auto lightRenderer = find(uuid, &objectUUID);
  if (!lightRenderer)
  {
    return;
  }

  lightRenderer->setDirection({ x, y, z });

  BindingContext::recordComponentEdit(objectUUID, lightRenderer);
}

void LightRendererBindingsProvider::bindSetConeAngle(const char* uuid, const float coneAngle)
{
  uuids::uuid objectUUID;
  const auto lightRenderer = find(uuid, &objectUUID);
  if (!lightRenderer)
  {
    return;
  }

  lightRenderer->setConeAngle(coneAngle);

  BindingContext::recordComponentEdit(objectUUID, lightRenderer);
}

bool LightRendererBindingsProvider::bindHas(const char* uuid)
{
  return find(uuid) != nullptr;
}
