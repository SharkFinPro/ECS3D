#ifndef SAVEUI_H
#define SAVEUI_H

#include <VulkanEngine/components/window/Window.h>
#include <functional>
#include <memory>
#include <string>

class ProjectSerializer;

namespace vke {
  class VulkanEngine;
}

// The editor-only half of the old SaveManager: native file dialogs + the Ctrl+S keybind + drag-and-drop
// loading. Save serializes the editor's replicated project straight to disk. Open/New apply the project
// locally (instant feedback) AND forward the blob via a callback the EditorApp turns into a loadProject
// command, so the authoritative server reloads + re-snapshots and everyone stays in sync.
class SaveUI {
public:
  using LoadProjectCallback = std::function<void(const std::string& projectJson)>;

  SaveUI(ProjectSerializer* projectSerializer, std::shared_ptr<vke::VulkanEngine> renderer);

  ~SaveUI();

  void setLoadProjectCallback(LoadProjectCallback callback);

  // When false (the connected server isn't in edit mode), dragging a project file in is ignored with a
  // note rather than loading it, since loading replaces the server's project.
  void setEditable(bool editable);

  void save();

  void saveAs();

  void open();

  void loadFromFile(const std::string& path);

  void createNewProject();

private:
  // Apply a project blob locally (instant feedback) + forward it to the authoritative server.
  void loadProjectBlob(const std::string& projectJson) const;

  ProjectSerializer* m_projectSerializer;

  std::shared_ptr<vke::VulkanEngine> m_renderer;

  LoadProjectCallback m_onLoadProject;

  std::string m_saveFile;

  bool m_editable = true;

  vke::EventListener<vke::KeyCallbackEvent> m_keyCallbackEventListener;
  vke::EventListener<vke::DropEvent> m_dropEventListener;

  [[nodiscard]] bool chooseSaveFile();

  [[nodiscard]] bool createSaveFile();

  void registerWindowEvents();
};



#endif //SAVEUI_H
