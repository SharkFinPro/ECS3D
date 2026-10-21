# Coin Rush

A 1-4 player arena game that shows the engine's authoritative client/server model: one host runs the
simulation, up to three friends join it, and everyone sees the same world and the same scores.

## The game

Each player spawns at a colored corner of a 60 x 60 floor that floats over the void. Coins appear across the
arena, and walking through one scores a point. Two sky islands hold bonus coins worth 3, reached by the lifts at
the north and south edges. Two purple pushers slide back and forth and shove anyone in their way, boulders roll
off the central dais every few seconds, and bodies bump each other, so knocking a rival into the void is a
strategy. Falling off respawns you at your corner.

A round lasts 90 seconds. The highest score wins and is written to the log, scores reset after a 5-second
break, and the next round starts.

The engine has no on-screen text, so each score shows as a pillar in the player's color beside their spawn
point, and as a `Scores: ...` line in the log. The Console panel shows the log in the editor.

## Controls

| Action | Default key |
| --- | --- |
| Move | W, A, S, D |
| Jump | Space |
| Look | Hold the right mouse button and drag |

The keys are fields on the Player prefab's `PlayerScript`, so a project can change them in the inspector.

## Playing it

Host the game by running a server on the sample folder, then join it with a client. Paths are relative to
the engine's build directory, where the sample is copied to `templates/coin-rush`.

```
ECS3DServer --project <engine dir>/templates/coin-rush --port 3000
ECS3DClient --host 127.0.0.1 --port 3000
```

Each friend joins with their own client, using the host's address on your network:

```
ECS3DClient --host <host's LAN address> --port 3000
```

The first time the server runs, your operating system's firewall will ask whether to let it accept network
connections. Allow it on private networks, or friends cannot reach the host.

To play alone, start a client on the folder. It starts its own local server, and the game is a coin hunt
against the clock:

```
ECS3DClient --project <engine dir>/templates/coin-rush
```

## Editing it

Open the folder in the editor to change the arena. The scene scripts (Scene Scripts window) hold the round
length, the break between rounds, and whether players are tinted in their slot color. The project is
a template, so the editor opens it as an untitled project and its first Save asks for a new folder.

Only `scripts/ScorePillar.cs` is specific to this sample. Everything else is built from the engine's starter
scripts and the built-in assets.
