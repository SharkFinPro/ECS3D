#ifndef COMPONENTREGISTRY_H
#define COMPONENTREGISTRY_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

class Component;

class ComponentRegistry {
public:
  using Factory = std::function<std::shared_ptr<Component>()>;

  void registerComponent(const std::string& typeName, Factory factory);

  [[nodiscard]] std::shared_ptr<Component> create(const std::string& typeName) const;

  [[nodiscard]] bool isRegistered(const std::string& typeName) const;

private:
  std::unordered_map<std::string, Factory> m_factories;
};



#endif //COMPONENTREGISTRY_H
