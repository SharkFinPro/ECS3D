// Port of source/EngineConfig.h — window/camera/ImGui/rendering startup configuration.
//
// DEVIATION: `window.fullscreen` and `window.resizable` have no meaning for a canvas that is
// sized by CSS, so they are dropped; `title` sets document.title. `rendering.rayTracingEnabled`
// is dropped — WebGPU has no ray tracing (WebGPUPortGuide.md §1).

import { Vec3 } from "./utilities/Math";

export interface WindowConfig {
  width: number;
  height: number;
  title: string;
}

export interface CameraConfig {
  position: Vec3;
  speed: number;
}

export interface ImGuiConfig {
  useDockspace: boolean;
  sceneViewName: string;
}

export interface EngineConfig {
  window: WindowConfig;
  camera: CameraConfig;
  imGui: ImGuiConfig;
}

// The defaults from EngineConfig.h's member initializers. Tests spread over this the same way
// upstream tests brace-initialize only the fields they care about.
export const DEFAULT_ENGINE_CONFIG: EngineConfig = {
  window: {
    width: 1280,
    height: 720,
    title: "WebGPU Engine",
  },
  camera: {
    position: [0, 0, 0],
    speed: 1.0,
  },
  imGui: {
    useDockspace: true,
    sceneViewName: "Scene View",
  },
};

// Merges a test's partial config over the defaults (the analogue of designated-initializer
// syntax in the upstream `const vke::EngineConfig ENGINE_CONFIG { ... }`).
export function resolveEngineConfig(config?: DeepPartial<EngineConfig>): EngineConfig {
  return {
    window: { ...DEFAULT_ENGINE_CONFIG.window, ...config?.window },
    camera: { ...DEFAULT_ENGINE_CONFIG.camera, ...config?.camera },
    imGui: { ...DEFAULT_ENGINE_CONFIG.imGui, ...config?.imGui },
  };
}

export type DeepPartial<T> = { [K in keyof T]?: Partial<T[K]> };
