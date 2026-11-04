#!/bin/bash
# Runs a reference script with the game rules reset first, so a rule an earlier script left in the save
# (bonemeal.txt once left random_tick_speed 0, which silently spoiled every later ice/snow/crop run) cannot spoil this one.
#   bash tools/refscripts/runref.sh scripts/<name>.txt      (path relative to E:/mc-reference)
set -e
src="${1:?usage: runref.sh scripts/<name>.txt}"
[ -f "/e/mc-reference/$src" ] && src="/e/mc-reference/$src"
name="$(basename "$src")"
mkdir -p /e/mc-reference/scripts/reset
out="/e/mc-reference/scripts/reset/$name"
printf '%s\n' "cmd gamerule random_tick_speed 3" "cmd gamerule do_weather_cycle true" "cmd gamerule advance_time true" "cmd gamerule fall_damage true" "cmd gamerule mob_griefing true" > "$out"
cat "$src" >> "$out"
exec bash /e/mc-reference/ref.sh "$out"
