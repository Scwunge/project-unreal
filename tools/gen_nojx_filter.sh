#!/bin/bash
# Writes the "nojx" automation filter: every McClient test group except McServer.JavaExport.
# Generated from the test sources, so a new group is picked up without editing a list.
# Usage: bash tools/gen_nojx_filter.sh [out]   (default /e/mc-remote/filter-nojx.txt)
# Run:   bash /e/mc-remote/remote.sh test "$(cat /e/mc-remote/filter-nojx.txt)"
cd "$(dirname "$0")/.." || exit 1
OUT="${1:-/e/mc-remote/filter-nojx.txt}"
grep -rhoE '"McClient\.[A-Za-z0-9_]+\.[A-Za-z0-9_]+' --include=*.cpp Source | tr -d '"' | sort -u \
  | grep -v '^McClient\.McServer\.JavaExport$' | paste -sd+ - | tr -d '\n' > "$OUT"
echo "$(tr '+' '\n' < "$OUT" | wc -l) groups -> $OUT"
