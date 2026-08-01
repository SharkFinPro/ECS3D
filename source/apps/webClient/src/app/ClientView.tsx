"use client";

// The React half of the client: owns the canvas, drives ClientApp.frame from requestAnimationFrame,
// and shows connection state. There is deliberately no menu, server browser or login here - the C++
// ECS3DClient connects immediately on start (ClientApp's constructor does it), and this mirrors that.
// The server address is configuration, not UI, exactly as --host/--port are there.

import { useEffect, useRef, useState } from "react";
import { ClientApp, type ClientStatus } from "../ClientApp";
import { SceneStatus } from "../data/scenes/SceneManager";
import { defaultPort } from "../net/Protocol";

// Mirrors ClientApp::ConnectOptions' defaults (127.0.0.1, net::defaultPort).
const host = process.env.NEXT_PUBLIC_ECS3D_HOST || "127.0.0.1";
const port = Number(process.env.NEXT_PUBLIC_ECS3D_PORT) || defaultPort;

type Phase = "starting" | "running" | "failed";

export default function ClientView() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [phase, setPhase] = useState<Phase>("starting");
  const [error, setError] = useState<string | null>(null);
  const [status, setStatus] = useState<ClientStatus | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) {
      return;
    }

    // StrictMode mounts, unmounts and remounts in development. `cancelled` makes the aborted first
    // pass tear its own engine down instead of leaving a second device and WebSocket alive.
    let cancelled = false;
    let app: ClientApp | null = null;
    let frameHandle = 0;
    let statusHandle = 0;
    let onKeyDown: ((event: KeyboardEvent) => void) | null = null;

    const start = async () => {
      if (!navigator.gpu) {
        setError(
          "This browser has no WebGPU support. The client renders through WebGPU and has no fallback path.",
        );
        setPhase("failed");
        return;
      }

      try {
        const created = await ClientApp.create(canvas);

        if (cancelled) {
          created.dispose();
          return;
        }

        app = created;

        // Only now, past the cancelled check. Connecting inside create() would give StrictMode's
        // discarded first mount a socket too, and since the server assigns the lowest free player
        // slot, that shifts this client up one - breaking both its camera and its input routing.
        created.connect({ host, port });
        setPhase("running");
        setStatus(created.getStatus());

        // Debugging handle, mirroring webGPUTest's window.__wgeRenderer. Reaches the whole live tree
        // (scene, registries, engine) from the console.
        (globalThis as unknown as { __ecs3dClient?: ClientApp }).__ecs3dClient = created;

        const frame = (timeMs: number) => {
          frameHandle = requestAnimationFrame(frame);
          created.frame(timeMs);
        };
        frameHandle = requestAnimationFrame(frame);

        // Polled rather than pushed: the status is read every frame anyway, and re-rendering React
        // at 60Hz for a small readout would be pure waste.
        statusHandle = window.setInterval(() => setStatus(created.getStatus()), 500);

        // Backquote toggles freecam. Deliberately not a letter: A-Z, Space and the arrows are all
        // forwarded to the server as GLFW key codes, so a letter would also mean something in game.
        onKeyDown = (event: KeyboardEvent) => {
          if (event.code === "Backquote") {
            event.preventDefault();
            created.setFreecam(!created.isFreecam());
            setStatus(created.getStatus());
          }
        };
        window.addEventListener("keydown", onKeyDown);
      } catch (cause) {
        if (cancelled) {
          return;
        }

        console.error("[Client] Failed to start:", cause);
        setError(cause instanceof Error ? cause.message : String(cause));
        setPhase("failed");
      }
    };

    void start();

    return () => {
      cancelled = true;
      cancelAnimationFrame(frameHandle);
      window.clearInterval(statusHandle);
      if (onKeyDown) {
        window.removeEventListener("keydown", onKeyDown);
      }
      app?.dispose();
    };
  }, []);

  const toggleFreecam = () => {
    const app = (globalThis as unknown as { __ecs3dClient?: ClientApp }).__ecs3dClient;
    if (!app) {
      return;
    }

    app.setFreecam(!app.isFreecam());
    setStatus(app.getStatus());
  };

  return (
    <main style={{ position: "fixed", inset: 0 }}>
      <canvas ref={canvasRef} />

      {phase === "failed" && (
        <Overlay title="Could not start">
          <p style={{ maxWidth: 460, lineHeight: 1.5 }}>{error}</p>
        </Overlay>
      )}

      {phase === "starting" && <Overlay title="Starting renderer…" />}

      {phase === "running" && status && (
        <StatusBar status={status} onToggleFreecam={toggleFreecam} />
      )}
    </main>
  );
}

function Overlay({ title, children }: { title: string; children?: React.ReactNode }) {
  return (
    <div
      style={{
        position: "absolute",
        inset: 0,
        display: "grid",
        placeItems: "center",
        alignContent: "center",
        gap: 12,
        background: "#0d0d12",
        textAlign: "center",
        padding: 24,
      }}
    >
      <h1 style={{ fontSize: 15, fontWeight: 600 }}>{title}</h1>
      <div style={{ fontSize: 13, opacity: 0.65 }}>{children}</div>
    </div>
  );
}

const connectionLabel: Record<ClientStatus["connection"], { text: string; tone: "ok" | "bad" | "wait" }> =
  {
    connecting: { text: `connecting to ${host}:${port}…`, tone: "wait" },
    connected: { text: `${host}:${port}`, tone: "ok" },
    failed: { text: `no server at ${host}:${port}`, tone: "bad" },
  };

function StatusBar({
  status,
  onToggleFreecam,
}: {
  status: ClientStatus;
  onToggleFreecam: () => void;
}) {
  const connection = connectionLabel[status.connection];

  return (
    <div
      style={{
        position: "absolute",
        left: 12,
        bottom: 12,
        display: "flex",
        gap: 14,
        alignItems: "center",
        padding: "7px 12px",
        borderRadius: 6,
        background: "rgba(13, 13, 18, 0.72)",
        border: "1px solid rgba(232, 232, 238, 0.1)",
        fontSize: 12,
        fontVariantNumeric: "tabular-nums",
        pointerEvents: "none",
      }}
    >
      <Field label={connection.text} tone={connection.tone} />
      <Field label={`${status.fps} fps`} />
      <Field label={status.sceneName ?? "no scene"} />
      <Field label={`${status.objectCount} objects`} />
      <Field
        // A slot with no matching PlayerController means the view has fallen back to another
        // camera and this client's input reaches nothing - it reads as a bug, so call it out.
        label={
          status.playerSlot < 0
            ? "no slot"
            : status.missingPlayerObject
              ? `slot ${status.playerSlot} (no player object)`
              : `slot ${status.playerSlot}`
        }
        tone={status.missingPlayerObject ? "bad" : undefined}
      />
      <Field label={SceneStatus[status.sceneStatus]} />

      <button
        type="button"
        onClick={onToggleFreecam}
        title="Detach the view from your player object (`)"
        style={{
          pointerEvents: "auto",
          cursor: "pointer",
          font: "inherit",
          padding: "2px 8px",
          borderRadius: 4,
          border: "1px solid rgba(232, 232, 238, 0.18)",
          background: status.freecam ? "rgba(110, 231, 168, 0.16)" : "transparent",
          color: status.freecam ? "#6ee7a8" : "inherit",
          opacity: status.freecam ? 1 : 0.7,
        }}
      >
        {status.freecam ? "freecam" : "player cam"}
      </button>
    </div>
  );
}

function Field({ label, tone }: { label: string; tone?: "ok" | "bad" | "wait" }) {
  const color =
    tone === "ok" ? "#6ee7a8" : tone === "bad" ? "#f0846c" : tone === "wait" ? "#e8c06e" : undefined;

  return <span style={{ color, opacity: color ? 1 : 0.7 }}>{label}</span>;
}
