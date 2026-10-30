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
    for sign in scene.get("signs", []):
        x, y, z = sign["at"]
        sides = []
        for key, prefix in (("front_text", ""), ("back_text", "back_")):
            lines = list(sign.get("front" if not prefix else "back", []))[:4]
            lines += [""] * (4 - len(lines))
            messages = ",".join(json.dumps(line) for line in lines)
            glowing = "1b" if sign.get(prefix + "glowing", False) else "0b"
            color = sign.get(prefix + "color", "black")
            sides.append(f"{key}:{{color:\"{color}\",has_glowing_text:{glowing},messages:[{messages}]}}")
        waxed = "1b" if sign.get("waxed", False) else "0b"
        print(f"data merge block {x} {y} {z} {{{','.join(sides)},is_waxed:{waxed}}}")
    x, y, z, yaw, pitch = scene["camera"]
    # The camera is the eye position; /tp places the feet, 1.62 blocks lower.
    print(f"tp @s {x} {y - 1.62:.2f} {z} {yaw} {pitch}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
