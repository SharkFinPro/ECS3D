#ifndef OBJECTMANAGER_H
#define OBJECTMANAGER_H

#include <vector>
#include <memory>

class Object;

class ObjectManager {
public:
  ObjectManager();

  void update(float dt);

private:
  std::vector<std::shared_ptr<Object>> objects;

  void variableUpdate(float dt);
  void fixedUpdate(float dt);
};



#endif //OBJECTMANAGER_H
