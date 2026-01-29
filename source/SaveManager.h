#ifndef ECS3D_SAVEMANAGER_H
#define ECS3D_SAVEMANAGER_H

#include <nlohmann/json.hpp>

class ECS3D;

class SaveManager {
public:
  explicit SaveManager(ECS3D* ecs);

  void save();

  void saveAs();

  void loadSaveFile(const std::string& saveFile);

  void loadSaveFile();

private:
  ECS3D* m_ecs;

  std::string m_saveFile;

  [[nodiscard]] nlohmann::json readSaveDataFile() const;

  bool chooseSaveFile();

  bool createSaveFile();

  void registerSaveHotkeys();

  void loadFromSaveFile() const;
};


#endif //ECS3D_SAVEMANAGER_H