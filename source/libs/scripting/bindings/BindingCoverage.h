#ifndef BINDINGCOVERAGE_H
#define BINDINGCOVERAGE_H

#include <objects/components/Component.h>
#include <array>

// Closes the loop between "a ComponentType exists" and "scripts can reach it": nothing else connects
// Component.h to the *Bindings providers below, so a component could ship unreachable from C# scripts
// with nobody noticing. Every ComponentType enumerator needs exactly one row here, either bound (a
// provider registers it with the managed side in ScriptEngine::registerBindings) or an explicit,
// commented opt-out. There is no default/catch-all row: componentBindingCoverageIsComplete() fails the
// static_assert below the moment a new enumerator is added without a matching row.
enum class BindingStatus {
  bound,       // a *Bindings provider exposes this component to scripts today
  notYetBound, // no bindings yet; not a design decision, just not built
  nativeOnly   // deliberately not scriptable, or not itself an addressable component
};

struct BindingCoverageEntry {
  ComponentType type;
  BindingStatus status;
};

constexpr std::array<BindingCoverageEntry, static_cast<size_t>(ComponentType::count)> kComponentBindingCoverage {{
  { ComponentType::transform,                      BindingStatus::bound },        // TransformBindings
  { ComponentType::modelRenderer,                  BindingStatus::notYetBound },  // ModelRendererBindings landing on another branch
  { ComponentType::rigidBody,                      BindingStatus::bound },        // RigidBodyBindings
  { ComponentType::collider,                       BindingStatus::notYetBound },  // no ColliderBindings yet
  { ComponentType::lightRenderer,                  BindingStatus::notYetBound },  // no LightRendererBindings yet
  { ComponentType::SubComponentType_none,          BindingStatus::nativeOnly },   // a collider's default subtype, not itself an addressable component
  { ComponentType::SubComponentType_boxCollider,   BindingStatus::notYetBound },  // no ColliderBindings yet
  { ComponentType::SubComponentType_sphereCollider,BindingStatus::notYetBound },  // no ColliderBindings yet
  { ComponentType::script,                         BindingStatus::nativeOnly },   // the scripting system itself; a script doesn't bind to its own component
  { ComponentType::playerController,               BindingStatus::nativeOnly },   // read internally to resolve a player's input slot (InputUtilsBindings), no direct script surface
  { ComponentType::camera,                         BindingStatus::bound }         // CameraBindings
}};

// The table is filled positionally: row i must describe ComponentType i, not just appear somewhere in
// the array. Checking that directly also subsumes duplicate detection - if any type were listed twice,
// some other index could not hold its own type's row.
constexpr bool componentBindingCoverageIsComplete()
{
  for (size_t i = 0; i < kComponentBindingCoverage.size(); ++i)
  {
    if (kComponentBindingCoverage[i].type != static_cast<ComponentType>(i))
    {
      return false;
    }
  }

  return true;
}

// A failure here means a ComponentType was added (or reordered) without a matching row at its index in
// kComponentBindingCoverage - add one (bound, notYetBound, or nativeOnly) in this file, in enum order.
static_assert(componentBindingCoverageIsComplete(),
              "A ComponentType enumerator (Component.h) has no row at its own index in "
              "kComponentBindingCoverage - add one (bound, notYetBound, or nativeOnly) in "
              "source/libs/scripting/bindings/BindingCoverage.h, in enum order.");

#endif //BINDINGCOVERAGE_H
