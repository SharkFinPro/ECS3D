"use client";

import dynamic from "next/dynamic";

// The renderer is loaded client-side only. `"use client"` alone is not enough - Next still runs a
// client component through a server render, and the engine tree evaluates browser-only WebGPU
// globals (GPUShaderStage, GPUColorWrite, GPUBufferUsage, ...) while building the module-level
// pipeline state presets. `ssr: false` keeps that whole tree out of the server bundle, which is also
// the honest thing to do: there is no adapter, device or canvas on the server.
const ClientView = dynamic(() => import("./ClientView"), {
  ssr: false,
  loading: () => (
    <main
      style={{
        position: "fixed",
        inset: 0,
        display: "grid",
        placeItems: "center",
        background: "#0d0d12",
        fontSize: 13,
        opacity: 0.6,
      }}
    >
      Starting client…
    </main>
  ),
});

export default function Home() {
  return <ClientView />;
}
