#ifndef SAVEUI_H
#define SAVEUI_H

#include <VulkanEngine/components/window/Window.h>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

class ProjectSerializer;

namespace vke {
  class VulkanEngine;
}

// Editor file I/O: native file dialogs + drag-and-drop loading. save()/saveAs() are invoked by the
// KeybindDispatcher (Ctrl+S / Ctrl+Shift+S), wired in EditorApp rather than here. Save serializes the
// editor's replicated project straight to disk. Open/New apply the project locally (instant feedback)
// AND notify via a callback the EditorApp turns into a loadProject command (packing its now-current
// managers), so the authoritative server reloads + re-snapshots and everyone stays in sync.
//
// Also owns the "unsaved changes" gate: New, Open, a dropped project file, and closing the window all
// discard whatever is currently loaded, so they all route through guardDiscard() rather than acting
// immediately. requestNewProject()/requestOpen() are the guarded entry points EditorApp's menu calls;
// the drop listener and the window-close callback guard themselves the same way.
class SaveUI {
public:
  using LoadProjectCallback = std::function<void()>;

  SaveUI(ProjectSerializer* projectSerializer, std::shared_ptr<vke::VulkanEngine> renderer);

  ~SaveUI();

  void setLoadProjectCallback(LoadProjectCallback callback);

  // When false (the connected server isn't in edit mode), dragging a project file in is ignored with a
  // note rather than loading it, since loading replaces the server's project.
  void setEditable(bool editable);

  // Returns false if no destination was chosen (a Save As dialog the user canceled) or the write failed,
  // so a caller discarding the current project afterward knows not to proceed.
  [[nodiscard]] bool save();

  void saveAs();

  void open();

  void loadFromFile(const std::string& path);

  void createNewProject();

  // New/Open, guarded by the unsaved-changes prompt: if the project is dirty, the request is deferred
  // until the modal resolves; otherwise it runs immediately.
  void requestNewProject();

  void requestOpen();

  // Every send-path callback in EditorApp (editComponent/sceneEdit/addAsset/renameAsset/removeAsset)
  // calls this so the gate knows the loaded project no longer matches what's on disk. This only counts
  // edits *this* editor sent - an edit from another editor connected to the same server arrives as an
  // incoming message, not through these callbacks, so it isn't reflected here.
  void markEdited();

  [[nodiscard]] bool isDirty() const;

  // Draws the "unsaved changes" modal when a guarded action is pending. Called every frame, regardless
  // of whether the rest of the GUI is shown, so a window-close prompt can't be hidden behind a toggled-off
  // UI.
  void displayUnsavedChangesModal();

private:
  // What to do once the unsaved-changes prompt (if any) has been resolved.
  enum class PendingDiscard {
    none,
    newProject,
    open,
    loadFile,
    closeWindow
  };

  // Apply a project blob locally (instant feedback) + forward it to the authoritative server.
  void loadProjectBlob(const std::string& projectJson);

  // Runs the action directly if the project isn't dirty; otherwise defers it behind the modal.
  void guardDiscard(PendingDiscard action, std::string path = "");

  void performDiscard(PendingDiscard action, const std::string& path);

  static void windowCloseCallback(GLFWwindow* window);

  ProjectSerializer* m_projectSerializer;

  std::shared_ptr<vke::VulkanEngine> m_renderer;

  LoadProjectCallback m_onLoadProject;

  std::string m_saveFile;

  bool m_editable = true;

  vke::EventListener<vke::DropEvent> m_dropEventListener;

  // Bumped by markEdited(), snapshotted into m_savedEditCount on a successful save/load/new. Dirty
  // whenever the two disagree - the simplest signal available since the project has no version counter
  // of its own (AssetRegistry's tracks asset content only, not scene edits).
  size_t m_editCount = 0;
  size_t m_savedEditCount = 0;

  PendingDiscard m_pendingDiscard = PendingDiscard::none;
  std::string m_pendingLoadPath;
  bool m_showUnsavedChangesModal = false;

  [[nodiscard]] bool chooseSaveFile();

  [[nodiscard]] bool createSaveFile();

  void registerWindowEvents();
};



#endif //SAVEUI_H
