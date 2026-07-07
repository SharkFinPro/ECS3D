#include "WorldBindings.h"
#include "BindingContext.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <string>
#include <utility>

namespace {
  // Returned strings point into this buffer, valid until the next WorldBindings call on the same
  // thread. Scripts run synchronously on the server loop thread, so the managed caller marshals the
  // result to a C# string immediately (see World.cs) before the next binding call overwrites it.
  thread_local std::string s_returnBuffer;

  const char* store(std::string value)
  {
    s_returnBuffer = std::move(value);
    return s_returnBuffer.c_str();
  }
}

WorldBindings WorldBindingsProvider::getBindings()
{
  return WorldBindings {
    .findObjectByName = &bindFindObjectByName,
    .getObjectName = &bindGetObjectName,
    .objectExists = &bindObjectExists,
    .getAllObjectUuids = &bindGetAllObjectUuids
  };
}

const char* WorldBindingsProvider::bindFindObjectByName(const char* name)
{
  const auto objectManager = BindingContext::getObjectManager();
  if (!objectManager || !name)
  {
    return store("");
  }

  const std::string target(name);
  for (const auto& object : objectManager->getAllObjects())
  {
    if (object->getName() == target)
    {
      return store(uuids::to_string(object->getUUID()));
    }
  }

  return store("");
}

const char* WorldBindingsProvider::bindGetObjectName(const char* uuid)
{
  const auto objectManager = BindingContext::getObjectManager();
  if (!objectManager || !uuid)
  {
    return store("");
  }

  const auto parsed = uuids::uuid::from_string(std::string(uuid));
  if (!parsed.has_value())
  {
    return store("");
  }

  const auto object = objectManager->getObjectByUUID(parsed.value());
  if (!object)
  {
    return store("");
  }

  return store(object->getName());
}

bool WorldBindingsProvider::bindObjectExists(const char* uuid)
{
  const auto objectManager = BindingContext::getObjectManager();
  if (!objectManager || !uuid)
  {
    return false;
  }

  const auto parsed = uuids::uuid::from_string(std::string(uuid));
  if (!parsed.has_value())
  {
    return false;
  }

  return objectManager->getObjectByUUID(parsed.value()) != nullptr;
}

const char* WorldBindingsProvider::bindGetAllObjectUuids()
{
  const auto objectManager = BindingContext::getObjectManager();
  if (!objectManager)
  {
    return store("");
  }

  // UUIDs never contain commas, so a comma-delimited list marshals back cleanly (see World.cs).
  std::string result;
  for (const auto& object : objectManager->getAllObjects())
  {
    if (!result.empty())
    {
      result += ',';
    }
    result += uuids::to_string(object->getUUID());
  }

  return store(result);
}
