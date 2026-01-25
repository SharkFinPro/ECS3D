#ifndef ECS3D_SAVEMANAGER_H
#define ECS3D_SAVEMANAGER_H

#include <nlohmann/json.hpp>

class ECS3D;

class SaveManager {
public:
  explicit SaveManager(ECS3D* ecs);

  void update();

private:
  ECS3D* m_ecs;

  nlohmann::json m_saveData;

  bool m_wasSaving = false;

  void save() const;

  void readSaveDataFile();

  static void createSaveDataFile();
};


#endif //ECS3D_SAVEMANAGER_H