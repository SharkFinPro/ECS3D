#ifndef ECS3D_SAVEMANAGER_H
#define ECS3D_SAVEMANAGER_H

#include <nlohmann/json.hpp>

class ECS3D;

class SaveManager {
public:
  SaveManager(ECS3D* ecs,
              std::string saveFile);

  void save();

  void saveAs();

private:
  ECS3D* m_ecs;

  std::string m_saveFile;

  [[nodiscard]] nlohmann::json readSaveDataFile() const;

  bool chooseSaveFile();

  bool createSaveFile();

  void registerSaveHotkeys();
};


#endif //ECS3D_SAVEMANAGER_H