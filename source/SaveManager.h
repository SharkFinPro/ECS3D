#ifndef ECS3D_SAVEMANAGER_H
#define ECS3D_SAVEMANAGER_H

#include <nlohmann/json.hpp>
#include <VulkanEngine/utilities/EventSystem.h>
#include <VulkanEngine/components/window/Window.h>

class ECS3D;

class SaveManager {
public:
  explicit SaveManager(ECS3D* ecs);

  ~SaveManager();

  void save();

  void saveAs();

  void loadSaveFile(const std::string& saveFile);

  void loadSaveFile();

  void createNewProject();

private:
  ECS3D* m_ecs;

  std::string m_saveFile;

  vke::EventListener<vke::KeyCallbackEvent> m_keyCallbackEventListener;
  vke::EventListener<vke::DropEvent> m_dropEventListener;

  [[nodiscard]] nlohmann::json readSaveDataFile() const;

  [[nodiscard]] bool chooseSaveFile();

  [[nodiscard]] bool createSaveFile();

  void registerWindowEvents();

  void loadFromSaveFile();
};


#endif //ECS3D_SAVEMANAGER_H