# Starter Scripts

ECS3D ships 17 ready-made C# scripts in `source/libs/scripting/UserScripts/Starters/`. Drop one on an object
(or add it to a scene) and it does something visible with no edits. Read them as examples, copy them, or use
them as they are.

It assumes you have read [Your first script](your-first-script.md) and
[Scene scripts and events](scene-scripts-and-events.md).

## How to read this page

- **Object script** means the class derives from `ScriptBase` and goes on an object. **Scene script** means it
  derives from `SceneScript` and goes in the Scene Scripts panel.
- The field tables use the exact label the Inspector shows (the text in `[ExposeToEditor("...")]`).
- Fields are in units per second (or degrees per second). The engine works per tick, and the scripts convert.
- Per-tick logic runs in `fixedUpdate(float dt)`, so behavior is the same on every machine.
- Objects are referred to by name, and prefabs and textures by uuid (universally unique identifier) string.
- A script that cannot find a named object logs one warning, does nothing, and looks again about once a second.
- Several scripts move an object's position or rotation directly. On an object that also has a `RigidBody`,
  most of them log one warning that physics and the script will fight. The notes below say which cooperate.

Showcase use lists the planned sample scenes that use each script: the Welcome scene, the Physics Playground
sample and the Coin Rush sample.

## Motion

### Spinner

Object script. Turns its object about an axis. On an object without a `RigidBody` it composes the rotation
itself; on one with a `RigidBody` it sets the angular velocity instead, so physics stays in charge. The axis
is in the world for a root object and in the parent's frame for a child.

| Label | Type | Default |
|-------|------|---------|
| Axis | vector | (0, 1, 0) |
| Degrees Per Second | float | 45 |

Events: none. Showcase use: used by Welcome (the Earth and the Spinner Rings) and Coin Rush (the coin
prefabs).

### Bobber

Object script. Moves its object back and forth along an axis on a sine wave around the position it had at
start. Positions are local, so a child bobs in its parent's frame. Warns on an object with a `RigidBody`.

| Label | Type | Default |
|-------|------|---------|
| Axis | vector | (0, 1, 0) |
| Amplitude | float | 0.5 |
| Frequency Hz | float | 0.25 |
| Phase Degrees | float | 0 |

Events: none. Showcase use: used by Welcome (the three accent lights) and Coin Rush (the coin prefabs).

### Orbiter

Object script. Circles a fixed point or a named object, following it if it moves, and can turn to face its
direction of travel. Use it on a root object: it reads the center in the world and writes its own position
locally, so a parented orbiter orbits in its parent's frame (it warns if it has a parent, or a `RigidBody`).
While a named center is missing the object holds still.

| Label | Type | Default |
|-------|------|---------|
| Center Object | string (object name) | empty |
| Center | vector | (0, 0, 0) |
| Radius | float | 5 |
| Period Seconds | float | 10 |
| Axis | vector | (0, 1, 0) |
| Start Angle | float (degrees) | 0 |
| Face Travel | bool | false |

Center is used when Center Object is empty. A Period Seconds of 0 or less leaves the object at its start
angle. Events: none. Showcase use: used by Welcome (the Moon around the Earth).

### LookAtTarget

Object script. Turns so its forward direction (-Z) faces a named object or a player's body, at a capped turn
rate. Use it on a root object: a child's aim is in its parent's frame. Warns on an object with a `RigidBody`.

| Label | Type | Default |
|-------|------|---------|
| Target | string (object name) | empty |
| Target Player Slot | int | -1 |
| Turn Speed | float (degrees per second) | 180 |
| Yaw Only | bool | true |

A Target Player Slot of 0 or more targets that player's body and wins over Target. A Turn Speed of 0 snaps
straight to the target. Yaw Only keeps the object level and turns about the vertical axis only.

Events: none. Showcase use: used by Welcome (the Watcher, which tracks the player).

### Follower

Object script. Moves toward a named object or a player's body and stops inside a distance of it. With a
`RigidBody` it steers the body's velocity the way the player script does, so it collides and can be pushed;
without one it steps its position. Use it on a root object: positions are read in the world and moved in the
parent's frame.

| Label | Type | Default |
|-------|------|---------|
| Target | string (object name) | empty |
| Target Player Slot | int | -1 |
| Speed | float (units per second) | 4 |
| Stop Distance | float | 2 |
| Ignore Height | bool | true |

A Target Player Slot of 0 or more wins over Target. Ignore Height measures and moves on the ground plane
only, and on a body leaves the vertical velocity to physics.

Events: none. Showcase use: used by Welcome (Buddy, who trots after the player).

### PingPongMover

Object script. Moves between its start position and start plus an offset (both in the parent's frame),
pausing at each end with a timer, and can carry bodies standing on top of it. Warns on an object with a
`RigidBody`: use it on a kinematic object. Carried riders must be root objects.

| Label | Type | Default |
|-------|------|---------|
| Offset | vector | (0, 4, 0) |
| Speed | float (units per second along the offset) | 2 |
| Pause Seconds | float | 1 |
| Ease | bool | true |
| Carry Riders | bool | true |

Ease applies smoothstep to each leg; a leg takes the same time either way. Carry Riders tracks bodies in
contact (collision enter and exit) whose position is above the platform's top and moves each by the
platform's own displacement every tick. It moves their position, not their velocity, so a player can still
walk along a moving platform. Turn it off for a platform that should shove players instead.

Events: none. Showcase use: used by Welcome (the Lift and the Ferry) and Coin Rush (the lifts and pushers).

## Physics and interaction

### BouncePad

Object script. On a collision enter with a body that has a `RigidBody`, sets that body's velocity along a
direction to a fixed speed. It replaces the speed along that direction rather than adding to it, so every
bounce reaches the same height. Objects without a `RigidBody` (static geometry) are ignored. The colliding
object itself must carry the `RigidBody`; a child collider of a compound body is not bounced.

| Label | Type | Default |
|-------|------|---------|
| Strength | float (units per second) | 12 |
| Direction | vector | (0, 1, 0) |
| Cooldown | float (seconds) | 0.2 |

Cooldown is per other body, in fixed time. Events: none. Showcase use: used by Welcome (the Bounce Pad) and
Physics Playground (the bounce pads station, strengths 8, 14 and 20).

### Spawner

Object script. Every interval it spawns a prefab at its position plus a random point in a horizontal disc.
It keeps at most Max Alive of its spawns and destroys the oldest first when it would exceed that. Objects
that something else destroyed are forgotten. A Max Alive below 1 spawns nothing and warns. Interval and Seed
are read at start; the other fields apply live.

| Label | Type | Default |
|-------|------|---------|
| Prefab | string (prefab uuid) | the default project's Sphere prefab |
| Interval | float (seconds) | 1 |
| Max Alive | int | 10 |
| Offset Radius | float | 2 |
| Height Offset | float | 0 |
| Initial Velocity | vector (units per second) | (0, 0, 0) |
| Spawn On Start | bool | true |
| Seed | int | 0 |

Initial Velocity is applied to a spawn that has a `RigidBody`. A Seed of 0 picks a different sequence each
run; any other value repeats the same offsets. Events: none. Showcase use: used by Welcome (the ball, capsule
and dice spawners), Physics Playground (friction slopes, bounce pads, ramp race and shape rain) and Coin
Rush (coins, big coins and boulders).

### Lifetime

Object script. Destroys its object after a number of seconds of running time; paused time does not count.
Seconds is read at start.

| Label | Type | Default |
|-------|------|---------|
| Seconds | float | 5 |

Events: none. Showcase use: not used by the planned scenes; it is meant for your own spawned prefabs.

### Explosion

Object script. On a fresh press of a key (by any player), or every so often on a timer, finds bodies with a
`RigidBody` within a radius and gives each a velocity change away from the center. The push falls off
linearly to zero at the edge and is tilted upward. The script's own object is skipped, and it logs how many
bodies it hit.

| Label | Type | Default |
|-------|------|---------|
| Key | string | E |
| Radius | float | 7 |
| Strength | float (units per second at the center) | 18 |
| Upward Bias | float | 0.3 |
| Cooldown | float (seconds) | 1.5 |
| Fire Every | float (seconds) | 0 |

Key accepts a letter A to Z, Space, Left, Right, Up or Down (not case sensitive); an unknown name logs one
warning and leaves the key unbound. A Fire Every of 0 turns the timer off. Cooldown applies to both triggers.

Events: none. Showcase use: used by Physics Playground (the explosion pit).

### Collectible

Object script. Touching a player publishes a collected event, then the pickup destroys itself. Give its
collider the trigger flag so players pass through it. Objects without a `PlayerController` are ignored, and
it publishes only once.

| Label | Type | Default |
|-------|------|---------|
| Value | int | 1 |
| Event Name | string | collected |

Publishes: `collected` (or the name in Event Name) with a `CollectedEvent`. Showcase use: used by Coin Rush
(the coin prefabs).

### LaserPointer

Object script. Each tick casts a ray from its object along a direction, turned by the object's rotation and
ignoring the object itself. It moves a marker object to the hit point and hides the marker on a miss. When
the hit object changes it logs the new object's name (or "nothing").

| Label | Type | Default |
|-------|------|---------|
| Direction | vector (local to the object) | (0, 0, -1) |
| Max Distance | float | 50 |
| Marker | string (object name) | empty |
| Log Changes | bool | true |

The marker needs a `ModelRenderer` for hiding to work; with no Marker the script still logs changes. Events:
none. Showcase use: used by Welcome (the Watcher, with a marker on whatever it looks at).

### ColorOnHit

Object script. Swaps its `ModelRenderer` texture on the first contact and restores the original texture when
the last contact ends. It counts contacts, so overlapping hits do not revert early. It warns if the object has
no `ModelRenderer`. The change replicates to every client.

| Label | Type | Default |
|-------|------|---------|
| Hit Texture | string (texture uuid) | the palette_red texture |
| Revert On Exit | bool | true |
| Players Only | bool | false |

Events: none. Showcase use: used by Welcome (the Bounce Pad) and Physics Playground (the bounce pads).

## Game flow

These are all scene scripts. They talk to each other and to object scripts through the events listed in
[Events](#events).

### ScoreKeeper

Scene script. Adds each collected event's value to that player's score. On every change it logs a leaderboard
line such as `Scores: P0 4, P1 2` and publishes a score-changed event. Reaching a positive Target Score once
publishes a match-won event and logs the winner. On round over it logs the round's winner (or a tie) and,
when Reset On Round Over is on, sets every score to zero and publishes each zero. A player who leaves is
dropped without a score-changed event. Other scripts can read a score with `getScore(slot)`.

| Label | Type | Default |
|-------|------|---------|
| Event Name | string | collected |
| Target Score | int | 0 |
| Reset On Round Over | bool | true |

A Target Score of 0 or less means the match has no target. Event Name is read at start.

Subscribes to: the Event Name event (`CollectedEvent`) and `roundOver` (`RoundOverEvent`). Publishes:
`scoreChanged` and `matchWon`. Showcase use: used by Coin Rush.

### PlayerSpawnManager

Scene script. Gives every joining player a body. If a body already holds that player's slot (a placed
player), it adopts it; otherwise it spawns the player prefab at the next spawn point, names it `Player <slot>`
and sets the new body's slot in the same tick so the spawn replicates correctly. A spawned body is destroyed
when its player leaves; an adopted one is left alone. Spawn points are the objects whose names start with the
prefix, sorted by name. A join takes the point with the fewest live players, and each player already on it
lifts the newcomer 3 units. With no spawn points it spawns at the origin and warns once.

Each tick, a body below the Respawn Below Y height is moved back to its home with zero velocity. A spawned
body destroyed by something else is respawned after 2 seconds. A prefab that cannot be spawned logs a warning
and drops that player.

| Label | Type | Default |
|-------|------|---------|
| Player Prefab | string (prefab uuid) | the default project's Player prefab |
| Spawn Point Prefix | string | SpawnPoint |
| Spawn Height | float | 1 |
| Respawn Below Y | float | -20 |
| Tint Players | bool | false |

Tint Players sets the body's texture by slot: red, blue, green, orange, repeating from slot 4.

Publishes: `playerSpawned`. Showcase use: used by Welcome, Physics Playground (the hub) and Coin Rush, and by
the First-Person template.

### RoundTimer

Scene script. Counts a round down once a second, logs at 60, 30 and 10 seconds and each of the last five,
and publishes a tick each second. At zero it publishes round over. With Auto Restart on, it waits out the
intermission, starts the next round and publishes round started. A Round Seconds of 0 or less runs no round
and warns. With Start On Load off, call the script's `beginRound()` yourself to start a round.

| Label | Type | Default |
|-------|------|---------|
| Round Seconds | int | 90 |
| Intermission Seconds | float | 5 |
| Auto Restart | bool | true |
| Start On Load | bool | true |

The first round publishes no round-started event, because other scripts may not have started yet.

Publishes: `roundTick`, `roundOver` and `roundStarted`. Showcase use: used by Coin Rush.

### DayNightCycle

Scene script. Moves a named light along a vertical east-west arc (in the X and Y plane) around a center, one
full cycle per period. The color blends from night to dusk to day with the light's height, ambient and
diffuse scale with it (so the scene is never fully dark), and a spot light is pointed at the arc's center. It
warns if the light has no `LightRenderer` (only the position then changes) or has a `RigidBody`. On stop it
puts the light's diffuse back.

| Label | Type | Default |
|-------|------|---------|
| Light | string (object name) | Sun |
| Period Seconds | float | 120 |
| Arc Radius | float | 40 |
| Arc Center | vector | (0, 0, 0) |
| Start Time | float (0 to 1) | 0.3 |
| Day Color | color vector | (1, 0.96, 0.88) |
| Dusk Color | color vector | (1, 0.5, 0.2) |
| Night Color | color vector | (0.1, 0.15, 0.4) |
| Day Ambient | float | 0.3 |
| Night Ambient | float | 0.06 |

Start Time is the point in the day: 0 is sunrise in the east (+X), 0.25 noon, 0.5 sunset in the west and
0.75 midnight.

Events: none. Showcase use: used by Welcome.

## Events

The names and payloads live in `StarterEvents.cs`, so every script agrees on one shape. Subscribe with the
typed overload, for example `subscribe<CollectedEvent>(StarterEvents.Collected, onCollected)`.

| Event name | Constant | Payload | Published by | Subscribed by |
|------------|----------|---------|--------------|---------------|
| `collected` | `Collected` | `CollectedEvent`: `Slot`, `Value`, `CollectorUuid` | Collectible | ScoreKeeper |
| `scoreChanged` | `ScoreChanged` | `ScoreChangedEvent`: `Slot`, `Score` | ScoreKeeper | none of the starters |
| `matchWon` | `MatchWon` | `MatchWonEvent`: `Slot`, `Score` | ScoreKeeper | none of the starters |
| `roundTick` | `RoundTick` | `RoundTickEvent`: `SecondsLeft` | RoundTimer | none of the starters |
| `roundOver` | `RoundOver` | `RoundOverEvent`: `Round` | RoundTimer | ScoreKeeper |
| `roundStarted` | `RoundStarted` | `RoundStartedEvent`: `Round` | RoundTimer | none of the starters |
| `playerSpawned` | `PlayerSpawned` | `PlayerSpawnedEvent`: `Slot`, `Uuid` | PlayerSpawnManager | none of the starters |

Collectible and ScoreKeeper let you change the `collected` name through their Event Name fields; both must
match. The Coin Rush sample adds a score pillar script of its own that listens for `scoreChanged`.

## Helpers

Two files hold shared code and are not scripts you attach.

- `StarterEvents.cs`: the event names and payload classes above.
- `StarterUtils.cs`: named-object lookup that retries at most once a second and warns once, a player-body
  lookup by slot, key-name parsing, and the Euler and quaternion conversions that match the engine's rotation
  convention.
