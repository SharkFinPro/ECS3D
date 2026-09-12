#ifndef OBJECT_H
#define OBJECT_H

#include "ObjectManager.h"
#include <nlohmann/json_fwd.hpp>
#include <vector>
#include <unordered_map>
#include <cstddef>
#include <memory>
#include <string>
#include <uuid.h>

namespace net {
  class Message;
  class MessageReader;
}

enum class ComponentType;
class Component;

// A wire or JSON payload can claim arbitrarily deep object nesting; recursing that deep in
// Object::unpack, Object::loadChildren, or ObjectManager::reassignUUIDs would exhaust the stack before
// any handler gets a chance to reject it. 64 is far beyond any authored hierarchy but leaves comfortable
// headroom under a Debug build's larger stack frames.
inline constexpr std::size_t maxObjectDepth = 64;

class Object : public std::enable_shared_from_this<Object> {
public:
  explicit Object(std::string name = "Object");

  explicit Object(const std::vector<std::shared_ptr<Component>>& components,
                  std::string name = "Object");

  Object(const nlohmann::json& objectData,
         ObjectManager* manager);

  // depth is the nesting level of the children being built (the caller's own depth + 1); defaulted so
  // every existing call site still means "load the direct children of this object". Throws past
  // maxObjectDepth, the same guard unpack applies to the wire path.
  void loadChildren(const nlohmann::json& childrenData, std::size_t depth = 1);

  void setParent(const std::shared_ptr<Object>& parent);

  [[nodiscard]] std::shared_ptr<Object> getParent() const;

  void addChild(std::shared_ptr<Object> child);

  void removeChild(const std::shared_ptr<Object>& child);

  [[nodiscard]] const std::vector<std::shared_ptr<Object>>& getChildren() const;

  void addComponent(const std::shared_ptr<Component>& component,
                    bool setOwner = true);

  void removeComponent(const std::shared_ptr<Component>& component);

  template<typename T>
  [[nodiscard]] std::shared_ptr<T> getComponent(ComponentType type) const;

  void setManager(ObjectManager* objectManager);
  [[nodiscard]] ObjectManager* getManager() const;

  [[nodiscard]] std::string getName() const;
  void setName(const std::string& name);

  void start();

  void stop();

  [[nodiscard]] nlohmann::json serialize();

  [[nodiscard]] uuids::uuid getUUID() const;

  // True when object sits somewhere below this one in the hierarchy. An object is not its own
  // ancestor, so a caller guarding against cycles has to check for self separately.
  [[nodiscard]] bool isAncestorOf(const std::shared_ptr<Object>& object) const;

  [[nodiscard]] const std::unordered_map<ComponentType, std::shared_ptr<Component>>& getComponents() const;

  [[nodiscard]] const std::vector<std::shared_ptr<Component>>& getScripts() const;

  void pack(net::Message& message) const;

  // depth is this object's own nesting level (root = 0); defaulted so every existing call site still
  // means "unpack this object". Throws past maxObjectDepth rather than recursing further.
  void unpack(net::MessageReader& messageReader, std::size_t depth = 0);

private:
  std::unordered_map<ComponentType, std::shared_ptr<Component>> m_components;
  std::vector<std::shared_ptr<Component>> m_scripts;

  ObjectManager* m_manager = nullptr;

  // Whether this object has been started, so a component added to it mid-run is started too.
  bool m_started = false;

  std::weak_ptr<Object> m_parent;

  std::vector<std::shared_ptr<Object>> m_children;

  uuids::uuid m_uuid;

  std::string m_name;

  [[nodiscard]] std::shared_ptr<Component> getComponent(ComponentType type) const;

  void loadFromJSON(const nlohmann::json& objectData);

  // The body of unpack, split out so unpack itself is only the stop/start bracket around it and can
  // restore the running state whichever way this exits.
  void unpackFields(net::MessageReader& messageReader, std::size_t depth);
};


template<typename T>
std::shared_ptr<T> Object::getComponent(const ComponentType type) const
{
  const auto component = getComponent(type);

  return component ? std::dynamic_pointer_cast<T>(component) : nullptr;
}

#endif //OBJECT_H
