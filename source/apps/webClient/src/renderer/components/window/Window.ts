// Port of source/components/window/Window.{h,cpp} — GLFW window becomes the canvas element.
//
// Same responsibilities as upstream: own the surface the engine draws into, latch input state
// once per frame so Camera can poll it, and emit the typed events other subsystems listen for
// (WebGPUPortGuide.md §2). GLFW's callback registration becomes DOM listeners; `glfwPollEvents`
// has no analogue because the browser delivers events between animation frames, so `update()`
// only does the per-frame latching that upstream does around the poll.
//
// DEVIATIONS: there is no "should close" flag on a canvas, so `isOpen()` is true until dispose();
// DropEvent is not wired up (nothing in the ported tests consumes it).

import { EngineConfig } from "../../EngineConfig";
import { EventSystem } from "../../utilities/EventSystem";

export interface ContentScaleEvent {
  xscale: number;
  yscale: number;
}

export interface FramebufferResizeEvent {
  width: number;
  height: number;
}

export interface KeyCallbackEvent {
  key: string;
  action: "press" | "release";
}

export interface ScrollEvent {
  xoffset: number;
  yoffset: number;
}

export interface WindowEventMap {
  contentScale: ContentScaleEvent;
  framebufferResize: FramebufferResizeEvent;
  keyCallback: KeyCallbackEvent;
  scroll: ScrollEvent;
}

// KeyboardEvent.code / MouseEvent.button values, named so call sites read like the GLFW_KEY_*
// and GLFW_MOUSE_BUTTON_* constants upstream.
export const Key = {
  w: "KeyW",
  a: "KeyA",
  s: "KeyS",
  d: "KeyD",
  space: "Space",
  leftShift: "ShiftLeft",
  escape: "Escape",
} as const;

export const MouseButton = {
  left: 0,
  middle: 1,
  right: 2,
} as const;

export class Window extends EventSystem<WindowEventMap> {
  private readonly canvas: HTMLCanvasElement;

  // Three positions, mirroring GLFW: `live*` is the cursor right now (what glfwGetCursorPos reads,
  // here kept current by the pointermove listener), `mouse*` is the snapshot taken for THIS frame,
  // and `previousMouse*` is the frame before it. Camera's swivel is the mouse - previousMouse
  // delta, so live and mouse must stay separate — collapsing them makes every delta zero.
  private previousMouseX = 0;
  private previousMouseY = 0;
  private mouseX = 0;
  private mouseY = 0;
  private liveMouseX = 0;
  private liveMouseY = 0;

  // `scroll` is the value visible to this frame; `pendingScroll` accumulates wheel deltas that
  // arrive between frames. update() moves one into the other, which is what clearing m_scroll
  // immediately before glfwPollEvents() achieves upstream.
  private scroll = 0;
  private pendingScroll = 0;

  private keysPressed = new Map<string, boolean>();
  private buttonsPressed = new Set<number>();

  private contentScale = 1.0;
  private open = true;

  private resizeObserver: ResizeObserver | null = null;
  private detach: (() => void) | null = null;

  constructor(canvas: HTMLCanvasElement, config: EngineConfig["window"]) {
    super();
    this.canvas = canvas;

    document.title = config.title;
    this.contentScale = window.devicePixelRatio || 1;

    this.attachListeners();
  }

  getCanvas(): HTMLCanvasElement {
    return this.canvas;
  }

  isOpen(): boolean {
    return this.open;
  }

  // Per-frame latch (the body of Window::update() minus glfwPollEvents).
  update(): void {
    this.scroll = this.pendingScroll;
    this.pendingScroll = 0;

    // Exactly Window::update()'s tail: shift last frame's snapshot into `previous`, then sample the
    // cursor's current position — which is glfwGetCursorPos upstream and `live*` here.
    this.previousMouseX = this.mouseX;
    this.previousMouseY = this.mouseY;
    this.mouseX = this.liveMouseX;
    this.mouseY = this.liveMouseY;
  }

  getFramebufferSize(): { width: number; height: number } {
    return { width: this.canvas.width, height: this.canvas.height };
  }

  keyIsPressed(key: string): boolean {
    return this.keysPressed.get(key) ?? false;
  }

  buttonIsPressed(button: number): boolean {
    return this.buttonsPressed.has(button);
  }

  getCursorPos(): { x: number; y: number } {
    return { x: this.mouseX, y: this.mouseY };
  }

  getPreviousCursorPos(): { x: number; y: number } {
    return { x: this.previousMouseX, y: this.previousMouseY };
  }

  getScroll(): number {
    return this.scroll;
  }

  getContentScale(): number {
    return this.contentScale;
  }

  dispose(): void {
    this.open = false;
    this.resizeObserver?.disconnect();
    this.resizeObserver = null;
    this.detach?.();
    this.detach = null;
  }

  private attachListeners(): void {
    const canvas = this.canvas;

    const onKeyDown = (e: KeyboardEvent) => {
      // Keys typed into a GUI field are not camera input (upstream ImGui captures them first).
      const target = e.target as HTMLElement | null;
      if (target && (target.tagName === "INPUT" || target.tagName === "SELECT" || target.tagName === "TEXTAREA")) {
        return;
      }
      this.keysPressed.set(e.code, true);
      if (e.code === Key.space) e.preventDefault();
      this.emit("keyCallback", { key: e.code, action: "press" });
    };
    const onKeyUp = (e: KeyboardEvent) => {
      this.keysPressed.set(e.code, false);
      this.emit("keyCallback", { key: e.code, action: "release" });
    };
    const onBlur = () => {
      this.keysPressed.clear();
      this.buttonsPressed.clear();
    };

    // Capture keeps a drag alive when the cursor leaves the canvas, which is what GLFW's cursor
    // grab does for the camera swivel. It can reject a pointer id that is no longer active, and an
    // uncaught throw here would take the button state down with it — so the button bookkeeping
    // always happens first, and the capture is best-effort.
    const onPointerDown = (e: PointerEvent) => {
      // Seed all three positions from this event, the way upstream's constructor seeds them with
      // glfwGetCursorPos. Without it, a press-and-drag as the very first interaction (no prior
      // pointermove to prime `live*`) yields a first delta measured from (0,0) and flings the
      // camera through a huge rotation on frame one.
      this.liveMouseX = e.clientX;
      this.liveMouseY = e.clientY;
      this.mouseX = e.clientX;
      this.mouseY = e.clientY;
      this.previousMouseX = e.clientX;
      this.previousMouseY = e.clientY;

      this.buttonsPressed.add(e.button);
      try {
        canvas.setPointerCapture(e.pointerId);
      } catch {
        // Pointer is gone or synthetic; the drag still works via normal event bubbling.
      }
    };
    const onPointerUp = (e: PointerEvent) => {
      this.buttonsPressed.delete(e.button);
      try {
        canvas.releasePointerCapture(e.pointerId);
      } catch {
        // Matching guard.
      }
    };
    // Stands in for the OS cursor position glfwGetCursorPos samples; update() snapshots it.
    const onPointerMove = (e: PointerEvent) => {
      this.liveMouseX = e.clientX;
      this.liveMouseY = e.clientY;
    };
    const onContextMenu = (e: Event) => e.preventDefault();
    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      // ~1 notch per 100px, wheel-up moves forward — the sign GLFW reports for yoffset.
      const yoffset = -e.deltaY / 100;
      this.pendingScroll += yoffset;
      this.emit("scroll", { xoffset: 0, yoffset });
    };

    window.addEventListener("keydown", onKeyDown);
    window.addEventListener("keyup", onKeyUp);
    window.addEventListener("blur", onBlur);
    canvas.addEventListener("pointerdown", onPointerDown);
    canvas.addEventListener("pointerup", onPointerUp);
    canvas.addEventListener("pointermove", onPointerMove);
    canvas.addEventListener("contextmenu", onContextMenu);
    canvas.addEventListener("wheel", onWheel, { passive: false });

    this.detach = () => {
      window.removeEventListener("keydown", onKeyDown);
      window.removeEventListener("keyup", onKeyUp);
      window.removeEventListener("blur", onBlur);
      canvas.removeEventListener("pointerdown", onPointerDown);
      canvas.removeEventListener("pointerup", onPointerUp);
      canvas.removeEventListener("pointermove", onPointerMove);
      canvas.removeEventListener("contextmenu", onContextMenu);
      canvas.removeEventListener("wheel", onWheel);
    };

    // glfwSetFramebufferSizeCallback / glfwSetWindowContentScaleCallback.
    this.resizeObserver = new ResizeObserver(() => {
      const scale = window.devicePixelRatio || 1;
      if (scale !== this.contentScale) {
        this.contentScale = scale;
        this.emit("contentScale", { xscale: scale, yscale: scale });
      }

      this.emit("framebufferResize", {
        width: Math.max(1, Math.floor(canvas.clientWidth * scale)),
        height: Math.max(1, Math.floor(canvas.clientHeight * scale)),
      });
    });
    this.resizeObserver.observe(canvas);
  }
}
