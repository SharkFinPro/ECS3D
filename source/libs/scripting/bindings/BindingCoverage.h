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

// True only if every enumerator in [0, ComponentType::count) appears in kComponentBindingCoverage
// exactly once. The array is sized off ComponentType::count, so a new enumerator without a matching row
// leaves a trailing entry value-initialized to { ComponentType::transform, BindingStatus::bound } - which
// this catches as a duplicate of the real transform row, rather than passing silently.
constexpr bool componentBindingCoverageIsComplete()
{
  std::array<bool, static_cast<size_t>(ComponentType::count)> seen{};

  for (const auto& entry : kComponentBindingCoverage)
  {
    const auto index = static_cast<size_t>(entry.type);
    if (seen[index])
    {
      return false;
    }
    seen[index] = true;
  }

  for (const bool wasSeen : seen)
  {
    if (!wasSeen)
    {
      return false;
    }
  }

  return true;
}

static_assert(componentBindingCoverageIsComplete(),
              "A ComponentType enumerator (Component.h) has no row in kComponentBindingCoverage - add "
              "one (bound, notYetBound, or nativeOnly) in source/libs/scripting/bindings/BindingCoverage.h.");

#endif //BINDINGCOVERAGE_H
