#ifndef COMPONENTEDITOR_H
#define COMPONENTEDITOR_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

class Component;

class ComponentEditor {
public:
  using GuiHandler = std::function<void(const std::shared_ptr<Component>&)>;

  void registerHandler(const std::string& typeName, GuiHandler handler);

  void displayGui(const std::string& typeName, const std::shared_ptr<Component>& component) const;

  [[nodiscard]] static bool displayHeader(const std::shared_ptr<Component>& component,
                                          const std::string& componentName = "");

private:
  std::unordered_map<std::string, GuiHandler> m_handlers;
};



#endif //COMPONENTEDITOR_H
