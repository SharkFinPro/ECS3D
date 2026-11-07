#ifndef RENDERCOMPONENTS_H
#define RENDERCOMPONENTS_H

#include <objects/Object.h>
#include <objects/components/Component.h>
#include <objects/components/LightRenderer.h>
#include <objects/components/ModelRenderer.h>
#include <objects/components/Transform.h>
#include <objects/components/collisions/BoxCollider.h>
#include <objects/components/collisions/CapsuleCollider.h>
#include <objects/components/collisions/ConvexMeshCollider.h>
#include <objects/components/collisions/SphereCollider.h>

// Non-owning pointers, valid only while the object's components are unchanged: use them within one loop iteration.
struct RenderComponents {
  Transform* transform = nullptr;
  ModelRenderer* modelRenderer = nullptr;
  LightRenderer* lightRenderer = nullptr;
  BoxCollider* box = nullptr;
  SphereCollider* sphere = nullptr;
  CapsuleCollider* capsule = nullptr;
  ConvexMeshCollider* convexMesh = nullptr;
};

// One pass over the object's own components instead of a map lookup, shared_ptr copy and dynamic cast per
// type. Reads only the object's own components (no rigid body, so no parent fallback). Each map key holds
// exactly one final class, so static_cast is exact.
[[nodiscard]] inline RenderComponents gatherRenderComponents(const Object& object)
{
  RenderComponents found;

  for (const auto& [type, component] : object.getComponents())
  {
    switch (type)
    {
      case ComponentType::transform:
        found.transform = static_cast<Transform*>(component.get());
        break;
      case ComponentType::modelRenderer:
        found.modelRenderer = static_cast<ModelRenderer*>(component.get());
        break;
      case ComponentType::lightRenderer:
        found.lightRenderer = static_cast<LightRenderer*>(component.get());
        break;
      case ComponentType::collider:
        switch (component->getSubType())
        {
          case ComponentType::SubComponentType_boxCollider:
            found.box = static_cast<BoxCollider*>(component.get());
            break;
          case ComponentType::SubComponentType_sphereCollider:
            found.sphere = static_cast<SphereCollider*>(component.get());
            break;
          case ComponentType::SubComponentType_capsuleCollider:
            found.capsule = static_cast<CapsuleCollider*>(component.get());
            break;
          case ComponentType::SubComponentType_convexMeshCollider:
            found.convexMesh = static_cast<ConvexMeshCollider*>(component.get());
            break;
          default:
            break;
        }
        break;
      default:
        break;
    }
  }

  return found;
}

#endif //RENDERCOMPONENTS_H
