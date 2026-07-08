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
// State is kept per player slot (Phase 3.2): the server binds each connection to a player slot and drops
// that connection's input into its slot, so two players no longer clobber one shared key set. A script
// reads its own player's input by resolving its object's PlayerController.playerSlot to a slot here
// (ScriptBase.input); the player-agnostic global InputUtils reads the aggregate across all slots.
//
// Set + read happen on the server's main loop thread (the inputState message is drained and the scripts
// run on the same thread), so no locking is needed.
class InputState {
public:
  // Per-slot writes: the server drops each player's inputState into its own slot.
  static void setKeysPressed(int32_t playerSlot, const std::vector<int>& keys);

  static void setFocused(int32_t playerSlot, bool focused);

  // Per-slot mouse write for one inputState message. Position and buttons are the latest values; delta
  // and scroll are per-frame amounts that ACCUMULATE across the messages received between ticks (so no
  // movement is lost when the client sends faster than the tick rate) and are zeroed by clearMouseDeltas.
  static void setMouse(int32_t playerSlot, float x, float y, float deltaX, float deltaY,
                       float scrollY, uint8_t buttons);

  // Reset every slot's accumulated mouse delta + scroll to zero. Called once per fixed tick after the
  // scripts consume them, so a still mouse reads zero delta rather than a stale value.
  static void clearMouseDeltas();

  // Drop a slot's state when its player disconnects, so a departed player's last keys don't linger.
  static void removeSlot(int32_t playerSlot);

  // Per-slot queries (used once a script knows which player it belongs to, via PlayerController).
  [[nodiscard]] static bool isKeyPressed(int32_t playerSlot, int key);

  [[nodiscard]] static bool isFocused(int32_t playerSlot);

  static void getMousePosition(int32_t playerSlot, float& x, float& y);

  static void getMouseDelta(int32_t playerSlot, float& deltaX, float& deltaY);

  [[nodiscard]] static float getScroll(int32_t playerSlot);

  // button is the GLFW mouse-button index (0 = left, 1 = right, 2 = middle).
  [[nodiscard]] static bool isMouseButtonPressed(int32_t playerSlot, int button);

  // Aggregate across every slot — what the player-agnostic global InputUtils reads. A key is "pressed"
  // if any player presses it; "focused" if any player is focused. In the singleplayer case (one slot)
  // this is identical to the old single-slot behavior.
  [[nodiscard]] static bool isAnyKeyPressed(int key);

  [[nodiscard]] static bool isAnyFocused();

private:
  struct SlotInput {
    std::unordered_set<int> pressedKeys;
    bool focused = false;

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float mouseDeltaX = 0.0f;  // accumulated since the last clearMouseDeltas
    float mouseDeltaY = 0.0f;
    float scrollY = 0.0f;      // accumulated since the last clearMouseDeltas
    uint8_t buttons = 0;
  };

  static std::unordered_map<int32_t, SlotInput> s_slots;
};



#endif //INPUTSTATE_H
