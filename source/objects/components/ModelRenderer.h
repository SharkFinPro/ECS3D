#ifndef MODELRENDERER_H
#define MODELRENDERER_H

#include "Component.h"
#include <VulkanEngine/objects/RenderObject.h>
#include <memory>

class ModelRenderer final : public Component {
public:
  ModelRenderer();
  ~ModelRenderer() override = default;

  void variableUpdate(float dt) override;

private:
  std::shared_ptr<RenderObject> renderObject;
};



#endif //MODELRENDERER_H
