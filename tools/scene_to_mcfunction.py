"""Turn a scene from tools/scenes/ into commands for real Minecraft 26.3, for side-by-side screenshots.

    python tools/scene_to_mcfunction.py tools/scenes/test.json > test.mcfunction

Make a classic_flat world in 26.3, run the commands (paste them, or put the file in a datapack's
function/ folder), then /tp to the camera line printed at the end and press F2. The game takes the
same scene from the same JSON with -McScene=test -McShot=<png>.
"""

from __future__ import annotations

import json
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 1
    with open(sys.argv[1], encoding="utf-8") as f:
        scene = json.load(f)

    print("gamerule doDaylightCycle false")
    print("gamerule doWeatherCycle false")
    print("time set noon")
    for box in scene["blocks"]:
        x1, y1, z1 = box["from"]
        x2, y2, z2 = box["to"]
        print(f"fill {x1} {y1} {z1} {x2} {y2} {z2} {box['state']}")
    x, y, z, yaw, pitch = scene["camera"]
    # The camera is the eye position; /tp places the feet, 1.62 blocks lower.
    print(f"tp @s {x} {y - 1.62:.2f} {z} {yaw} {pitch}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
