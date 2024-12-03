#ifndef OBJECTMANAGER_H
#define OBJECTMANAGER_H

#include <vector>
#include <memory>

class Object;
class ECS3D;

class ObjectManager {
public:
  ObjectManager();

  void update(float dt);

  void setECS(ECS3D* ecs);
  [[nodiscard]] ECS3D* getECS() const;

private:
  ECS3D* ecs;

  std::vector<std::shared_ptr<Object>> objects;

  const float fixedUpdateDt;
  float timeAccumulator;

  void variableUpdate(float dt);
  void fixedUpdate(float dt);
};



#endif //OBJECTMANAGER_H
