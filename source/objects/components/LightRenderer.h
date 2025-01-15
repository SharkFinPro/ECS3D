#ifndef LIGHTRENDERER_H
#define LIGHTRENDERER_H

#include "Component.h"
#include <memory>
#include <glm/glm.hpp>

class Transform;
class Light;

class LightRenderer final : public Component {
public:
  LightRenderer(glm::vec3 position, glm::vec3 color, float ambient, float diffuse, float specular);

  ~LightRenderer() override = default;

  void variableUpdate(float dt) override;

  void displayGui() override;

private:
  std::shared_ptr<Light> light;
  std::weak_ptr<Transform> transform_ptr;
};



#endif //LIGHTRENDERER_H
