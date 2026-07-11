#include "TransientObject.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <nlohmann/json.hpp>
#include <exception>
#include <utility>

TransientObject::TransientObject(std::shared_ptr<ComponentRegistry> componentRegistry)
  : m_componentRegistry(std::move(componentRegistry))
{}

// Defined here (not defaulted in the header) so the unique_ptr<ObjectManager> sees the complete type.
TransientObject::~TransientObject() = default;

bool TransientObject::syncFromBody(const std::string& body)
{
  if (m_loadedBody.has_value() && m_loadedBody.value() == body)
  {
    return false;
  }

  rebuild(body);
  return true;
}

void TransientObject::markSynced(const std::string& body)
{
  m_loadedBody = body;
}

ObjectManager* TransientObject::manager() const
{
  return m_manager.get();
}

const std::shared_ptr<Object>& TransientObject::object() const
{
  return m_object;
}

std::string TransientObject::serialize() const
{
  return m_object ? m_object->serialize().dump() : std::string{};
}

void TransientObject::rebuild(const std::string& body)
{
  // Remember the blob even if it fails to build, so a malformed/empty body isn't re-parsed every frame.
  m_loadedBody = body;
  m_manager.reset();
  m_object.reset();

  auto parsed = nlohmann::json::parse(body, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object())
  {
    return;
  }

  try
  {
    // A private manager owns the object exactly as a scene's does - but is never handed to any scene,
    // SceneManager, or replication path, so the object stays out of the simulation. Preserve the body's
    // uuids (no reassignUUIDs) so the body is stable across edits.
    m_manager = std::make_unique<ObjectManager>(m_componentRegistry);

    auto object = std::make_shared<Object>(parsed, m_manager.get());
    m_manager->addObject(object);

    // Object's ctor loads only its own components/scripts; children are separate objects (mirrors
    // ObjectManager::instantiateUnder), needed here so the whole subtree survives the re-serialize.
    if (parsed.contains("children"))
    {
      object->loadChildren(parsed.at("children"));
    }

    m_object = std::move(object);
  }
  catch (const std::exception&)
  {
    // A malformed body (unknown component type, missing uuid/name) - leave the object null; the inspector
    // shows its "unavailable" fallback.
    m_manager.reset();
    m_object.reset();
  }
}
