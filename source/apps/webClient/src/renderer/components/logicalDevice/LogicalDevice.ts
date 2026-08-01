// Port of source/components/logicalDevice/LogicalDevice.{h,cpp} — wraps the GPUDevice + queue.
//
// Collapses hard (WebGPUPortGuide.md §6): there is one queue, so the graphics/present/compute
// queue trio and every submit/fence helper are gone; so are the command-pool and descriptor-pool
// factories (WebGPU has neither). What remains is device ownership, the queue, and shader-module
// creation — which absorbs ShaderModule.cpp's file read since WGSL ships as text.

import { PhysicalDevice } from "../physicalDevice/PhysicalDevice";

export class LogicalDevice {
  private constructor(
    private readonly device: GPUDevice,
    private readonly physicalDevice: PhysicalDevice,
  ) {}

  static async create(): Promise<LogicalDevice> {
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) throw new Error("No WebGPU adapter found (is hardware acceleration enabled?).");

    const device = await adapter.requestDevice({ label: "vke.Device" });

    // Validation is always on in WebGPU and cannot be disabled, so this listener is the whole of
    // DebugMessenger.cpp: uncaptured errors arrive here with the `label` of the offending object.
    device.addEventListener("uncapturederror", (e) => {
      console.error("[vke] uncaptured WebGPU error:", (e as GPUUncapturedErrorEvent).error.message);
    });

    return new LogicalDevice(device, new PhysicalDevice(adapter));
  }

  getDevice(): GPUDevice {
    return this.device;
  }

  getPhysicalDevice(): PhysicalDevice {
    return this.physicalDevice;
  }

  getQueue(): GPUQueue {
    return this.device.queue;
  }

  destroy(): void {
    this.device.destroy();
  }
}
