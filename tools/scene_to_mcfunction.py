"""Turn a scene from tools/scenes/ into commands for real Minecraft 26.3, for side-by-side screenshots.

    python tools/scene_to_mcfunction.py tools/scenes/test.json > test.mcfunction
    python tools/scene_to_mcfunction.py --script tools/scenes/test.json > E:/mc-reference/scripts/scene_test.txt

Make a classic_flat world in 26.3, run the commands (paste them, or put the file in a datapack's
function/ folder), then /tp to the camera line printed at the end and press F2. The game takes the
same scene from the same JSON with -McScene=test -McShot=<png>.

With --script it writes an mc-reference script instead (run with E:/mc-reference/ref.sh): it clears the
scene's box, builds it without block updates (so a bed's halves or a lone wall sign stay put), goes to the
camera and saves ref_scene_<name>.png.
"""

from __future__ import annotations

import json
import os
import sys


def commands(scene: dict, strict: bool) -> list[str]:
    out = []
    flag = " strict" if strict else ""
    for box in scene["blocks"]:
        x1, y1, z1 = box["from"]
        x2, y2, z2 = box["to"]
        out.append(f"fill {x1} {y1} {z1} {x2} {y2} {z2} {box['state']}{flag}")
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
        out.append(f"data merge block {x} {y} {z} {{{','.join(sides)},is_waxed:{waxed}}}")
    return out


def camera_tp(scene: dict) -> str:
    x, y, z, yaw, pitch = scene["camera"]
    # The camera is the eye position; /tp places the feet, 1.62 blocks lower.
    return f"tp @s {x} {y - 1.62:.2f} {z} {yaw} {pitch}"


def main() -> int:
    args = sys.argv[1:]
    script = "--script" in args
    args = [a for a in args if a != "--script"]
    if len(args) != 1:
        print(__doc__, file=sys.stderr)
        return 1
    with open(args[0], encoding="utf-8") as f:
        scene = json.load(f)

    if not script:
        print("gamerule doDaylightCycle false")
        print("gamerule doWeatherCycle false")
        print("time set noon")
        for line in commands(scene, False):
            print(line)
        print(camera_tp(scene))
        return 0

    name = os.path.splitext(os.path.basename(args[0]))[0]
    xs = [v for b in scene["blocks"] for v in (b["from"][0], b["to"][0])]
    zs = [v for b in scene["blocks"] for v in (b["from"][2], b["to"][2])]
    x, y, z, yaw, pitch = scene["camera"]
    print(f"# mc-unreal's tools/scenes/{name}.json, from tools/scene_to_mcfunction.py --script.")
    print("cmd gamerule send_command_feedback false")
    print("cmd gamerule show_advancement_messages false")
    print(f"cmd execute in minecraft:overworld run tp @s {x} {y - 1.62:.2f} {z} {yaw} {pitch}")
    print("cmd effect give @s resistance 600 4 true")
    if scene.get("standing"):
        # The camera is a player standing on the floor (its eye 1.62 up), for what a spectator doesn't count as
        # (an enchanting table's book turns to and opens for players only).
        print("cmd gamemode creative")
        print("hud off")
    else:
        # A spectator floats where it is put, shows no hand, and with FOV effects off sees mc-unreal's 70 degrees.
        print("cmd gamemode spectator")
    print("fov steady")
    print("cmd weather clear")
    print("cmd time set noon")
    print("wait 100")
    print("cmd kill @e[type=!player]")
    # Clear round the scene down to the floor, then build it.
    print(f"cmd fill {min(xs) - 2} -60 {min(zs) - 2} {max(xs) + 2} -50 {max(zs) + 2} air strict")
    for line in commands(scene, True):
        print("cmd " + line)
    print("cmd " + camera_tp(scene))
    # Long enough for the chat to fade.
    print("wait 220")
    print(f"shot scene_{name}")
    # Give the shot time to be written and named before the client quits, and leave the saved player as other
    # scripts expect them.
    print("wait 20")
    print("cmd gamemode creative")
    return 0


if __name__ == "__main__":
    sys.exit(main())
