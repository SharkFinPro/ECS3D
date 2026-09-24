#ifndef PLAYERCONTROLLERBINDINGS_H
#define PLAYERCONTROLLERBINDINGS_H

#include <cstdint>

// PlayerController script bindings: check whether an object is bound to a player and read/change which
// slot it is bound to. Mirrors the LightRendererBindings pattern: a C-ABI struct of function pointers,
// resolved against the server's current ObjectManager via BindingContext. A script's own object is
// reached the same way as any other, by passing ScriptBase.EntityId as the uuid.
//
// setPlayerSlot goes through the component's own setter, so its rules still apply here (none today -
// PlayerController::setPlayerSlot accepts any int32_t). A set is only replicated when it actually
// changed the component's value, matching every other *Bindings setter.
struct PlayerControllerBindings
{
  bool(*has)(const char* uuid);
  int32_t(*getPlayerSlot)(const char* uuid);
  void(*setPlayerSlot)(const char* uuid, int32_t playerSlot);
};

class PlayerControllerBindingsProvider {
public:
  [[nodiscard]] static PlayerControllerBindings getBindings();

private:
  // find() resolves a uuid against the server's ObjectManager via BindingContext (set by ScriptSystem).
  static bool bindHas(const char* uuid);
  static int32_t bindGetPlayerSlot(const char* uuid);

  // Defers validation to the component's own setter and records the edit on BindingContext for
  // replication when the component was found and the value actually changed.
  static void bindSetPlayerSlot(const char* uuid, int32_t playerSlot);
};



#endif //PLAYERCONTROLLERBINDINGS_H
