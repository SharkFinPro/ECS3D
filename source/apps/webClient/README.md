# ECS3D Web Client

A browser port of `ECS3DClient` — the lightweight runtime view. It connects to an authoritative
`ECS3DServer` over WebSocket, renders the replicated scene with WebGPU, and sends input back.

It is not an editor and it does not simulate: physics, scripting and all authority stay on the server,
exactly as in the C++ client.

## Requirements

- **Node 20+** and a **WebGPU-capable browser** (Chrome/Edge 113+). There is no fallback renderer.
- A running **`ECS3DServer`** with the **WebSocket** transport selected — `Transport.cs`'s `Protocol`
  field. A browser cannot open the raw TCP socket the default backend uses. Both backends are
  wire-compatible, so this is a one-line change.

## Running

```bash
npm install
```

```bash
npm run dev
```

Then open http://localhost:3100. It connects on load — there is no menu or server browser, mirroring the
C++ client, which connects from its constructor.

(The dev server runs on **3100**, not Next's default 3000, because 3000 is `net::defaultPort` — the port
`ECS3DServer` itself listens on. Running both on one machine would otherwise collide.)

To point it at a different server, copy `.env.example` to `.env.local`:

```bash
cp .env.example .env.local
```

| Variable | Default | C++ equivalent |
|---|---|---|
| `NEXT_PUBLIC_ECS3D_HOST` | `127.0.0.1` | `--host` |
| `NEXT_PUBLIC_ECS3D_PORT` | `3000` | `--port` |

There is no singleplayer mode: the C++ client spawns a child `ECS3DServer` for that, and a web page
cannot start a process. Launch a server yourself first.

## Controls

Identical to the C++ client, because input is forwarded to the server verbatim and interpreted by the
same gameplay scripts:

- **WASD** — move; **Space** — up
- **Right-click drag** — look
- **Scroll** — zoom (free-fly camera only)

The view follows your player camera once the server assigns you a slot; until then it falls back to the
scene's first active camera, or the free-fly camera if there is none.

The status bar along the bottom shows the server address, framerate, scene, object count, your player
slot and the scene's run state.

**If it reads `slot N (no player object)` in red**, no object in the scene carries a `PlayerController`
for your slot. The view falls back to another camera and your input reaches nothing — so the player will
seem unresponsive. The server assigns the lowest free slot in connection order, so this usually means
more clients are connected than the scene defines players for (an editor session counts), or the current
scene has no player objects at all.

### Freecam

Press **`** (backquote), or click the **player cam / freecam** button in the status bar, to detach the
view from your player object and fly it freely. This mode does not exist in the C++ client.

While detached, the mouse is *not* sent to the server — right-drag and the wheel steer the camera
instead of turning your player. WASD still reaches the game, so you keep moving your player while you
fly. The freecam remembers where you left it, so toggling back and forth does not reset it.

## Assets

The server replicates asset *paths*, not bytes, so `public/assets/models` and `public/assets/textures`
mirror `source/apps/server/defaultAssets/`. If you add an asset to the server's default project, copy it
here too or it will silently fail to render.

## Notes for contributors

Read `AGENTS.md` before changing anything — especially the wire-format table. The protocol is a
byte-for-byte reimplementation of `Protocol.h`, with no versioning and no compile-time checking across
the language boundary, so an offset mistake shows up as a garbled scene rather than an error.
