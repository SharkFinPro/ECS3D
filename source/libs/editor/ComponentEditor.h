#ifndef COMPONENTEDITOR_H
#define COMPONENTEDITOR_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

class Component;

class ComponentEditor {
public:
  // A handler draws one component's editing widgets and returns whether the user changed anything this
  // frame (so the editor can emit a single edit command for the mutation).
  using GuiHandler = std::function<bool(const std::shared_ptr<Component>&)>;

  void registerHandler(const std::string& typeName, GuiHandler handler);

  [[nodiscard]] bool displayGui(const std::string& typeName, const std::shared_ptr<Component>& component) const;

  // Draws the CollapsingHeader for the component (and a "-" delete button for everything but the
  // Transform). Returns whether the header is open so the handler knows to draw the body.
  [[nodiscard]] static bool displayHeader(const std::shared_ptr<Component>& component,
                                          const std::string& componentName = "");

private:
  std::unordered_map<std::string, GuiHandler> m_handlers;
};



#endif //COMPONENTEDITOR_H
