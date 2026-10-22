# Physics Playground

A hands-on tour of the simulation. Each station isolates one behavior and stands on a thin colored pad.
Run it with a client, or open the folder in the editor:

```
ECS3DClient --project <path to this folder>
```

## Controls

- **WASD** to move, **Space** to jump, **right-drag** to look.
- **E** fires the explosion at the Explosion pit (any player's press counts).

You start at the hub in the middle, facing north.

## Stations

The domino run, pyramid and cradle row are one-shot: press Stop, then Play, to set them up again. The
other stations loop on their own.

- **Domino run** (north west): 30 dominoes on an S curve. The first leans 15 degrees; the last knocks a
  small sphere off a ledge.
- **Pyramid** (north): 30 unit cubes in layers of 16, 9, 4 and 1. It stands until you walk into it.
- **Cradle row** (north east): seven touching spheres; one more rolls down the feeder ramp on Play.
- **Friction slopes** (west): identical pucks with friction 0, 0.5 and 1 on 35 degree slopes. A new puck
  appears every 8 seconds.
- **Bounce pads** (east): pads with strengths 8, 14 and 20, each flashing red on contact, with balls
  dropping onto them.
- **Ramp race** (south west): a sphere, a drum, a hex log, a capsule lying across, a capsule lying along and
  a block go down a 25 degree ramp. Each lane releases a new body every 10 seconds; the hex log gets a
  small push downhill so it starts moving. The drum rolls on its own.
- **Explosion pit** (south): a heap of 25 small shapes; press E to blow it apart.
- **Shape rain** (south east): five spawners drop polyhedra into a basin; the pile holds at 60 shapes.

## Notes

Bodies released by a spawner appear exactly on their surface, tilted to lie flat on it. The Spawner gives the
hex log 3 units per second downhill, because its 30 degree facets would otherwise hold it still on the 25 degree ramp. The Capsule Along
prefab has friction 0.2 so it slides rather than holding inside the friction cone at 25 degrees.
