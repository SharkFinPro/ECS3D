#ifndef INPUTSTATE_H
#define INPUTSTATE_H

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// The networked input the headless server feeds to the scripts. A client/editor (which owns the GLFW
// window) captures the pressed keys each frame and sends them as an inputState message; ServerApp
// writes them here, and the InputUtils bindings read them.
//
// State is kept per connection (Phase 3.1): each connected client gets its own slot keyed by the stable
// connection id NetServer surfaces alongside inbound messages, so two players no longer clobber one
// shared key set. The (player-agnostic) script bindings still read the aggregate across all connections
// until Phase 3.2 gives a script its own player identity — a key reads as pressed if any connection is
// pressing it.
//
// Set + read happen on the server's main loop thread (the inputState message is drained and the scripts
// run on the same thread), so no locking is needed.
class InputState {
public:
  // Per-connection writes: the server drops each client's inputState into its own slot.
  static void setKeysPressed(int32_t connectionId, const std::vector<int>& keys);

  static void setFocused(int32_t connectionId, bool focused);

  // Per-connection queries (used once a script knows which player it belongs to — Phase 3.2).
  [[nodiscard]] static bool isKeyPressed(int32_t connectionId, int key);

  [[nodiscard]] static bool isFocused(int32_t connectionId);

  // Aggregate across every connection — the bridge the player-agnostic script bindings read until 3.2.
  // A key is "pressed" if any connection presses it; "focused" if any connection is focused. In the
  // singleplayer case (one connection) this is identical to the old single-slot behavior.
  [[nodiscard]] static bool isAnyKeyPressed(int key);

  [[nodiscard]] static bool isAnyFocused();

private:
  struct ConnectionInput {
    std::unordered_set<int> pressedKeys;
    bool focused = false;
  };

  static std::unordered_map<int32_t, ConnectionInput> s_connections;
};



#endif //INPUTSTATE_H
