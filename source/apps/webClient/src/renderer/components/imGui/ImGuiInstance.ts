// Port of source/components/imGui/ImGuiInstance.{h,cpp}.
//
// DEVIATION — this is the one subsystem that does not map to its upstream implementation. Dear
// ImGui has a first-party WebGPU backend (imgui_impl_wgpu, WebGPUPortGuide.md §6), but this port
// hosts the renderer inside a React page, so the panels are React components rather than
// immediate-mode draw calls inside the render pass.
//
// The API is shaped to keep the call sites identical anyway: a test declares controls the same
// way it calls ImGui::SliderFloat/Checkbox/ColorEdit3, once per window, and the React layer walks
// the registry. Each control keeps a getter and a setter that read and write live engine state,
// which reproduces immediate-mode semantics — a slider always shows the current value, and moving
// it writes straight through.
//
// The dockspace helpers (dockCenter/dockBottom/setBottomDockPercent) have no analogue: the React
// page owns layout. tests/common/gui.ts documents that where upstream calls setDockOptions().

export interface SliderControl {
  kind: "slider";
  label: string;
  min: number;
  max: number;
  step: number;
  get(): number;
  set(value: number): void;
}

export interface CheckboxControl {
  kind: "checkbox";
  label: string;
  get(): boolean;
  set(value: boolean): void;
}

export interface SelectControl {
  kind: "select";
  label: string;
  options: string[];
  get(): string;
  set(value: string): void;
}

export interface ColorControl {
  kind: "color";
  label: string;
  get(): [number, number, number]; // rgb in [0,1]
  set(value: [number, number, number]): void;
}

export interface Slider3Control {
  kind: "slider3";
  label: string;
  min: number;
  max: number;
  step: number;
  get(): [number, number, number];
  set(value: [number, number, number]): void;
}

export type GuiControl =
  | SliderControl
  | CheckboxControl
  | SelectControl
  | ColorControl
  | Slider3Control;

// Anything controls can be declared into: a window, or a collapsing header inside one.
export class GuiControlContainer {
  readonly controls: GuiControl[] = [];

  // ImGui::SliderFloat
  slider(
    label: string,
    min: number,
    max: number,
    step: number,
    get: () => number,
    set: (value: number) => void,
  ): this {
    this.controls.push({ kind: "slider", label, min, max, step, get, set });
    return this;
  }

  // ImGui::SliderInt — a slider with a step of 1.
  sliderInt(
    label: string,
    min: number,
    max: number,
    get: () => number,
    set: (value: number) => void,
  ): this {
    this.controls.push({ kind: "slider", label, min, max, step: 1, get, set });
    return this;
  }

  // ImGui::Checkbox
  checkbox(label: string, get: () => boolean, set: (value: boolean) => void): this {
    this.controls.push({ kind: "checkbox", label, get, set });
    return this;
  }

  // ImGui::BeginCombo / Selectable
  select(
    label: string,
    options: string[],
    get: () => string,
    set: (value: string) => void,
  ): this {
    this.controls.push({ kind: "select", label, options, get, set });
    return this;
  }

  // ImGui::ColorEdit3
  color(
    label: string,
    get: () => [number, number, number],
    set: (value: [number, number, number]) => void,
  ): this {
    this.controls.push({ kind: "color", label, get, set });
    return this;
  }

  // ImGui::SliderFloat3 — one row with three side-by-side drag boxes.
  slider3(
    label: string,
    min: number,
    max: number,
    step: number,
    get: () => [number, number, number],
    set: (value: [number, number, number]) => void,
  ): this {
    this.controls.push({ kind: "slider3", label, min, max, step, get, set });
    return this;
  }
}

// ImGui::CollapsingHeader(title) — a labelled, foldable group of controls inside a window. This is
// what lets each object/light own a section with plainly-named controls ("Position", "Specular"),
// exactly as displayObjectGui/displayLightGui do, instead of prefixing every label with the entry.
export class GuiHeader extends GuiControlContainer {
  constructor(readonly title: string) {
    super();
  }
}

// One ImGui::Begin(title) ... ImGui::End() block. A window can hold controls directly (Scene
// Options does) and/or a series of collapsing headers (Objects, Lights).
export class GuiWindow extends GuiControlContainer {
  readonly headers: GuiHeader[] = [];

  constructor(readonly title: string) {
    super();
  }

  collapsingHeader(title: string): GuiHeader {
    const existing = this.headers.find((header) => header.title === title);
    if (existing) return existing;

    const header = new GuiHeader(title);
    this.headers.push(header);
    return header;
  }
}

export class ImGuiInstance {
  readonly windows: GuiWindow[] = [];

  // ImGui::Begin(title): returns the existing window if the title was already opened this scene,
  // matching how ImGui merges repeated Begin() calls with the same name.
  begin(title: string): GuiWindow {
    const existing = this.windows.find((window) => window.title === title);
    if (existing) return existing;

    const window = new GuiWindow(title);
    this.windows.push(window);
    return window;
  }

  clear(): void {
    this.windows.length = 0;
  }
}
