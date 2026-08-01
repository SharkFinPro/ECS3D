// Port of source/libs/render/InputCapture.{h,cpp}.
//
// Captures the local keyboard + mouse so the client can ship it to the authoritative server as an
// inputState message (the server is headless and has no window of its own).
//
// The server's InputState is keyed by raw GLFW key codes - what the ScriptBridge Key enum expects -
// so this translates KeyboardEvent.code into those numbers rather than inventing an encoding.
// ROADMAP.md F1 (input action mapping) is what would eventually remove the raw codes.

import type { Window } from "../renderer/components/window/Window";

// Mouse button bits, matching GLFW_MOUSE_BUTTON_{LEFT,RIGHT,MIDDLE} (0/1/2) via (1 << button).
export const mouseButtonLeft = 1 << 0;
export const mouseButtonRight = 1 << 1;
export const mouseButtonMiddle = 1 << 2;

export interface InputSnapshot {
  keys: number[]; // currently-pressed GLFW key codes
  focused: boolean;
  mouseX: number;
  mouseY: number;
  mouseDeltaX: number;
  mouseDeltaY: number;
  scrollY: number;
  buttons: number;
}

// The keys the ScriptBridge Key enum exposes: space, A-Z, and the arrows - built in ascending GLFW
// code order so the snapshot compares equal frame to frame (ClientApp de-dups on it).
const polledKeys: { code: string; glfw: number }[] = [
  { code: "Space", glfw: 32 },
  ...Array.from({ length: 26 }, (_, i) => ({
    code: `Key${String.fromCharCode(65 + i)}`,
    glfw: 65 + i,
  })),
  { code: "ArrowRight", glfw: 262 },
  { code: "ArrowLeft", glfw: 263 },
  { code: "ArrowDown", glfw: 264 },
  { code: "ArrowUp", glfw: 265 },
];

// MouseEvent.button is left=0, middle=1, right=2; GLFW is left=0, right=1, middle=2. The bits below
// are GLFW's, so middle and right swap on the way across.
const buttonBits: { domButton: number; bit: number }[] = [
  { domButton: 0, bit: mouseButtonLeft },
  { domButton: 2, bit: mouseButtonRight },
  { domButton: 1, bit: mouseButtonMiddle },
];

export function capture(window: Window): InputSnapshot {
  const cursor = window.getCursorPos();
  const previous = window.getPreviousCursorPos();

  const keys: number[] = [];
  for (const { code, glfw } of polledKeys) {
    if (window.keyIsPressed(code)) {
      keys.push(glfw);
    }
  }

  let buttons = 0;
  for (const { domButton, bit } of buttonBits) {
    if (window.buttonIsPressed(domButton)) {
      buttons |= bit;
    }
  }

  return {
    keys,
    // Upstream this is informational: GLFW only reports keys while the window has focus, so the
    // state naturally goes quiet when the user tabs away. The DOM listeners are on `window` and
    // Window clears them on blur, giving the same behaviour.
    focused: typeof document === "undefined" ? true : document.hasFocus(),
    mouseX: cursor.x,
    mouseY: cursor.y,
    mouseDeltaX: cursor.x - previous.x,
    mouseDeltaY: cursor.y - previous.y,
    scrollY: window.getScroll(),
    buttons,
  };
}
