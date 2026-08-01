// Port of source/components/camera/Camera.{h,cpp} — the fly camera.
//
// Faithful down to the constants: speed 1.0 means 25 units/s of movement, 12.5 units per scroll
// notch and 0.25 degrees per pixel of right-drag swivel; yaw starts at 90 (looking down +Z) and
// pitch is clamped to +/-89.9. Like upstream it owns no input listeners — it polls Window each
// frame, so the key/mouse state and the swivel delta come from exactly the same place.
//
// The projection matrix is not here: it is built per-frame by RenderInfo (GraphicsPipeline.ts),
// matching upstream.

import { EngineConfig } from "../../EngineConfig";
import { Mat4, Vec3, mat4LookAt } from "../../utilities/Math";
import { Key, MouseButton, Window } from "../window/Window";

const UP: Vec3 = [0, 1, 0];

export class Camera {
  private position: Vec3;
  private direction: Vec3 = [0, 0, 1];

  private yaw = 90; // degrees; 90 => looking down +Z
  private pitch = 0;

  private speed = 0;
  private cameraSpeed = 0; // units per ms
  private scrollSpeed = 0; // units per scroll notch
  private swivelSpeed = 0; // degrees per pixel

  private enabled = true;
  private previousTime: number | null = null;

  constructor(config: EngineConfig["camera"]) {
    this.position = [...config.position] as Vec3;
    this.setSpeed(config.speed);
  }

  getViewMatrix(): Mat4 {
    const target: Vec3 = [
      this.position[0] + this.direction[0],
      this.position[1] + this.direction[1],
      this.position[2] + this.direction[2],
    ];
    return mat4LookAt(this.position, target, UP);
  }

  getPosition(): Vec3 {
    return [...this.position] as Vec3;
  }

  setPosition(position: Vec3): void {
    this.position = [...position] as Vec3;
  }

  setSpeed(cameraSpeed: number): void {
    this.speed = cameraSpeed * 50.0;

    this.cameraSpeed = this.speed * 0.0005;
    this.scrollSpeed = this.speed * 0.25;
    this.swivelSpeed = this.speed * 0.005;
  }

  enable(): void {
    this.enabled = true;
  }

  disable(): void {
    this.enabled = false;
  }

  isEnabled(): boolean {
    return this.enabled;
  }

  // Upstream measures dt from a steady_clock member; here the frame timestamp comes from
  // requestAnimationFrame. The first frame gets dt = 0 rather than a huge jump.
  processInput(window: Window, timeMs: number): void {
    const dt = this.previousTime === null ? 0 : Math.min(timeMs - this.previousTime, 100);
    this.previousTime = timeMs;

    this.handleRotation(window);
    this.handleZoom(window);
    this.handleMovement(window, dt);
  }

  private handleMovement(window: Window, dt: number): void {
    const yawRadians = (this.yaw * Math.PI) / 180;
    const length = Math.hypot(Math.sin(yawRadians), Math.cos(yawRadians)) || 1;
    const pDirection: Vec3 = [-Math.sin(yawRadians) / length, 0, Math.cos(yawRadians) / length];

    const step = this.cameraSpeed * dt;
    const move = (axis: Vec3, scale: number) => {
      this.position[0] += axis[0] * scale;
      this.position[1] += axis[1] * scale;
      this.position[2] += axis[2] * scale;
    };

    if (window.keyIsPressed(Key.w)) move(this.direction, step);
    if (window.keyIsPressed(Key.s)) move(this.direction, -step);

    if (window.keyIsPressed(Key.a)) move(pDirection, -step);
    if (window.keyIsPressed(Key.d)) move(pDirection, step);

    if (window.keyIsPressed(Key.space)) move(UP, step);
    if (window.keyIsPressed(Key.leftShift)) move(UP, -step);
  }

  private handleRotation(window: Window): void {
    if (window.buttonIsPressed(MouseButton.right)) {
      const cursor = window.getCursorPos();
      const previous = window.getPreviousCursorPos();

      this.yaw += (cursor.x - previous.x) * this.swivelSpeed;
      this.pitch -= (cursor.y - previous.y) * this.swivelSpeed;
      this.pitch = Math.min(89.9, Math.max(-89.9, this.pitch));
    }

    const yawRadians = (this.yaw * Math.PI) / 180;
    const pitchRadians = (this.pitch * Math.PI) / 180;
    this.direction = [
      Math.cos(yawRadians) * Math.cos(pitchRadians),
      Math.sin(pitchRadians),
      Math.sin(yawRadians) * Math.cos(pitchRadians),
    ];
  }

  private handleZoom(window: Window): void {
    const scroll = window.getScroll();
    if (scroll === 0) return;

    for (let i = 0; i < 3; i++) {
      this.position[i] += scroll * this.scrollSpeed * this.direction[i];
    }
  }
}
