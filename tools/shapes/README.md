# tools/shapes

Generator for the built-in default models and textures in `source/apps/server/defaultAssets/`.
Python 3, standard library only.

```
python tools/shapes/generate.py
```

The output is committed, so building the engine never needs Python. Output is deterministic: running the
generator twice produces identical bytes. Re-run it and commit the result whenever a builder changes.

- `glb.py` - one-mesh `.glb` writer (positions, normals, UVs, 16 or 32 bit indices) that refuses a triangle
  wound against its normals. `flat_faces` turns convex polygons into a flat-shaded mesh (one normal per face,
  vertices duplicated per face).
- `primitives.py` - `capsule`, `ramp`, `plane_tile`, `torus`.
- `textures.py` - the PNG writer and texture definitions.
- `library.py` - the convex shape library (D4, D8, D12, D20, Hex Prism, Drum, Gem, Boulder): the vertex
  formulas, a brute-force convex hull for small point sets that merges coplanar triangles into polygons, and
  flat-shaded models from them. The Boulder's points come from an explicit 32-bit xorshift with a fixed seed,
  so `DefaultShapes.cpp` reproduces them exactly. The D6 hull uses `cube_1x1x1.glb`.
- `generate.py` - the entry point. A new model adds a builder and an entry to `MODELS`.

## Conventions

- Every model spans -1 to 1 on its sized axes, so a scale is a half-extent.
- One mesh per file (the editor's Rebuild hull reads the first mesh), triangles only, explicit normals and
  `TEXCOORD_0`, counter-clockwise front faces.
- Hulls in `source/apps/server/DefaultShapes.cpp` are built from the same formulas as the models.

## Palette

| Texture | RGB |
| --- | --- |
| `palette_red` | 220, 50, 47 |
| `palette_green` | 60, 180, 75 |
| `palette_blue` | 50, 110, 220 |
| `palette_yellow` | 250, 205, 40 |
| `palette_orange` | 245, 130, 32 |
| `palette_purple` | 140, 70, 200 |
| `checker_orange` | 255, 150, 50 and 200, 90, 15 |
| `checker_blue` | 70, 130, 230 and 30, 70, 170 |
| `grid_checker` | background 214; thin lines 150; heavy lines (every 4 cells) 90 |

## Ground tiles

A floor is one invisible collider plus visible tiles, so the grid reads at 1 unit everywhere.

- The `Ground` object stays at unit scale. Its Box collider's own scale carries the floor's half-extents
  `(W/2, 1, D/2)`, and the object sits at y = -1, so the collider's top is y = 0. A collider's world scale is its
  own scale times the object's, so unit object scale leaves the collider scale as the half-extent.
- Tiles are children of `Ground` at local y = 1 and scale `(10, 1, 10)`. Each is 20 x 20 world units, because
  `plane_tile.glb` spans -1 to 1.
- `plane_tile.glb` has UVs 0 to 2.5 and `grid_checker.png` has 8 x 8 cells, so one tile shows 20 cells over
  20 units: cells are 1 unit. The renderer's sampler repeats UVs outside 0 to 1.
- Texture the tiles with `grid_checker.png` and place them edge to edge at 20-unit steps.
