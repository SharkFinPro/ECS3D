#ifndef ECS3D_SAVEMANAGER_H
#define ECS3D_SAVEMANAGER_H

#include <nlohmann/json.hpp>

class ECS3D;

class SaveManager {
public:
  explicit SaveManager(ECS3D* ecs);

  void save() const;

private:
  ECS3D* m_ecs;
  nlohmann::json m_saveData;

  void readSaveDataFile();

  static void createSaveDataFile();
};


#endif //ECS3D_SAVEMANAGER_H