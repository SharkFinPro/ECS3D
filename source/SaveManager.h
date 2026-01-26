#ifndef ECS3D_SAVEMANAGER_H
#define ECS3D_SAVEMANAGER_H

#include <nlohmann/json.hpp>

class ECS3D;

class SaveManager {
public:
  SaveManager(ECS3D* ecs,
              std::string saveFile);

  void update();

private:
  ECS3D* m_ecs;

  std::string m_saveFile;

  bool m_wasSaving = false;

  void save() const;

  [[nodiscard]] nlohmann::json readSaveDataFile() const;
};


#endif //ECS3D_SAVEMANAGER_H