#ifndef ECS3D_SAVEMANAGER_H
#define ECS3D_SAVEMANAGER_H

#include <nlohmann/json.hpp>

class SaveManager {
public:
  SaveManager();

private:
  nlohmann::json m_saveData;

  void readSaveData();
};


#endif //ECS3D_SAVEMANAGER_H