"""Writes the generated default assets into source/apps/server/defaultAssets/.

Run from anywhere: python tools/shapes/generate.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import primitives  # noqa: E402
import textures  # noqa: E402
from glb import write_glb  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
ASSETS = os.path.join(ROOT, "source", "apps", "server", "defaultAssets")

MODELS = {
    "capsule": primitives.capsule,
    "ramp": primitives.ramp,
    "plane_tile": primitives.plane_tile,
    "torus": primitives.torus,
}


def main():
    models = os.path.join(ASSETS, "models")
    tex = os.path.join(ASSETS, "textures")
    os.makedirs(models, exist_ok=True)
    os.makedirs(tex, exist_ok=True)

    for name, build in MODELS.items():
        write_glb(os.path.join(models, name + ".glb"), name, *build())

    textures.grid_checker(os.path.join(tex, "grid_checker.png"))
    textures.checker(os.path.join(tex, "checker_orange.png"), textures.CHECKER_ORANGE)
    textures.checker(os.path.join(tex, "checker_blue.png"), textures.CHECKER_BLUE)
    for name, color in textures.PALETTE.items():
        textures.solid(os.path.join(tex, "palette_%s.png" % name), color)


if __name__ == "__main__":
    main()
