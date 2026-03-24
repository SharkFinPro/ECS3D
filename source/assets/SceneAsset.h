#ifndef ECS3D_SCENEASSET_H
#define ECS3D_SCENEASSET_H

#include "Asset.h"

class ObjectManager;
class SceneManager;

class SceneAsset : public Asset {
public:
  explicit SceneAsset(SceneManager* sceneManager,
                      uuids::uuid uuid,
                      std::string name);

  void fixedUpdate(float dt) const;

  void variableUpdate() const;

  void start() const;

  void stop() const;

  nlohmann::json serialize() override;

private:
  SceneManager* m_sceneManager;

  std::shared_ptr<ObjectManager> m_objectManager;
};


#endif //ECS3D_SCENEASSET_H