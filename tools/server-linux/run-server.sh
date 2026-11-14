#!/bin/bash
# Starts the dedicated server. Run from the folder you unzipped (the one holding this file).
#   ./run-server.sh                 uses Saved/Server/server.properties (made with defaults on the first start)
#   ./run-server.sh -Port=25570     overrides server-port
# Stop with Ctrl+C, or `systemctl stop mcserver` when run as the service.
cd "$(dirname "$0")" || exit 1
EXE=$(ls McClient/Binaries/Linux/McClient-Linux-Shipping 2>/dev/null | head -1)
[ -x "$EXE" ] || chmod +x McClient/Binaries/Linux/McClient-Linux-Shipping 2>/dev/null
[ -x "$EXE" ] || { echo "McClient/Binaries/Linux/McClient-Linux-Shipping not found; run this from the unzipped folder."; exit 1; }
# The game's own copy of Minecraft's data (see README-linux.txt, "The cache"). -McCache= overrides the place.
CACHE="${MC_CACHE:-$PWD/McClient/cache}"
exec "$EXE" -server -nullrhi -nosound -unattended -stdout -FullStdOutLogOutput "-McCache=$CACHE" "$@"
