#ifndef ECS3D_SCENEASSET_H
#define ECS3D_SCENEASSET_H

#include "Asset.h"

class ObjectManager;
class SceneManager;

class SceneAsset : public Asset {
public:
  explicit SceneAsset(uuids::uuid uuid,
                      std::string name);

  void fixedUpdate(float dt) const;

  void variableUpdate() const;

  void start() const;

  void stop() const;

  void load() override;

  [[nodiscard]] nlohmann::json serialize() override;

  [[nodiscard]] std::shared_ptr<ObjectManager> getObjectManager() const;

private:
  std::shared_ptr<SceneManager> m_sceneManager;

  std::shared_ptr<ObjectManager> m_objectManager;
};


#endif //ECS3D_SCENEASSET_H