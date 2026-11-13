#!/usr/bin/env bash
# The playability harness on this PC (RTX 4060) instead of the 1070: same scripts, same record, runs named <name>_4060.
#
#   bash tools/perf-local.sh build [full]            compile Shipping (and the editor plus a cook with "full" or when no stage
#                                                    exists) in I:\mc-build\<checkout>-perf with J:\UE_5.9, under lock-blocal
#   bash tools/perf-local.sh run <name> <script> [game args]
#                                                    play tools/perf/<script>.txt in that stage on this desktop, holding
#                                                    lock-local (the desktop, in the queue with ref.sh and shots) and
#                                                    lock-blocal (no build beside it); record to Saved/Perf/<name>_4060/
#
# It takes no test lane: headless tests on this PC may run beside it, and machine.txt says what was running.
# MC_PERF_MINUTES sets a run's time limit (default 8); MC_PERF_CONFIG=Development for a log and the CSV profile.
set -u
ROOT="$(git rev-parse --show-toplevel)"; NAME="$(basename "$ROOT")"
B="I:/mc-build/$NAME-perf"; OUT="I:/mc-build/$NAME-perf-out"; WB="${B//\//\\}"; WO="${OUT//\//\\}"
ENGINE='J:\UE_5.9'
CONFIG="${MC_PERF_CONFIG:-Shipping}"
case "$CONFIG" in Shipping) STAGEDIR=stage ;; Test) STAGEDIR=stage-Test ;; Development) STAGEDIR=stage-Development ;; *) echo "MC_PERF_CONFIG is Shipping, Test or Development"; exit 2 ;; esac
# The installed engine builds no Test targets; Development (a log, stats and the CSV profiler) stands in, as McClient.exe.
EXE="McClient-Win64-$CONFIG.exe"; [ "$CONFIG" = Development ] && EXE=McClient.exe
CACHE='E:\mc-unreal\cache'
held=""; cleanup() { rm -f "${T1:-}"; for l in $held; do rm -rf "$l"; done; }; trap cleanup EXIT
take() { # queue dir or "", lock
  local q="$1" said=""
  until { [ -z "$q" ] || turn "$q"; } && mkdir "$2" 2>/dev/null; do
    [ -z "$said" ] && echo "waiting for $(basename "$2") ($(cat "$2/owner" 2>/dev/null))"; said=1; sleep 10
  done
  echo "$NAME perf-local $$" > "$2/owner"; held="$held $2"
}
turn() { local t p; for t in $(ls "$1" 2>/dev/null | sort); do p="${t##*-}"; if command ps -p "$p" >/dev/null 2>&1; then [ "$p" = "$$" ]; return; fi; rm -f "$1/$t"; done; return 0; }
mode="${1:-}"; shift || true
case "$mode" in
build)
  take "" /e/mc-remote/lock-blocal
  cd "$ROOT"; mkdir -p "$B" "$OUT" Saved
  echo "== sync $NAME -> $B"
  rm -rf "$B/Source" "$B/Config" "$B/Content" "$B/tools"
  { git ls-files Content tools Config; git ls-files -co --exclude-standard Source; echo McClient.uproject; } | tar -cf - -T - | tar -xf - -C "$B" || { echo "sync failed"; exit 1; }
  FULL=0; [ "${1:-}" = full ] && FULL=1
  [ -d "$OUT/$STAGEDIR/Windows/McClient/Content/Paks" ] || FULL=1
  if [ $FULL = 1 ]; then
    cmd //c "$ENGINE\\Engine\\Build\\BatchFiles\\Build.bat McClientEditor Win64 Development -Project=$WB\\McClient.uproject -WaitMutex -NoHotReloadFromIDE" > "$OUT/perf-build.log" 2>&1
    e=$?; echo "EDITOR_EXIT=$e"; [ $e = 0 ] || { tail -30 "$OUT/perf-build.log"; exit 1; }
    cmd //c "$ENGINE\\Engine\\Build\\BatchFiles\\RunUAT.bat BuildCookRun -project=$WB\\McClient.uproject -noP4 -platform=Win64 -clientconfig=$CONFIG -target=McClient -build -cook -stage -pak -archive -archivedirectory=$WO\\$STAGEDIR -utf8output -nocompileeditor" >> "$OUT/perf-build.log" 2>&1
    e=$?
  else
    cmd //c "$ENGINE\\Engine\\Build\\BatchFiles\\Build.bat McClient Win64 $CONFIG -Project=$WB\\McClient.uproject -WaitMutex" > "$OUT/perf-build.log" 2>&1
    e=$?; [ $e = 0 ] && cp "$B/Binaries/Win64/$EXE" "$OUT/$STAGEDIR/Windows/McClient/Binaries/Win64/"
  fi
  grep -E "error C|error LNK|: error |BUILD SUCCESSFUL|Total execution time" "$OUT/perf-build.log" | tail -20
  echo "BUILD_EXIT=$e"; [ $e = 0 ]
  ;;
run)
  run="${1}_4060"; script="$2"; shift 2; args="$*"
  [ -f "$ROOT/tools/perf/$script.txt" ] || { echo "no tools/perf/$script.txt"; exit 2; }
  [ -f "$OUT/$STAGEDIR/Windows/McClient/Binaries/Win64/$EXE" ] || { echo "no stage: run build first"; exit 2; }
  MINUTES="${MC_PERF_MINUTES:-8}"
  Q=/e/mc-remote/queue; mkdir -p "$Q"; T1="$Q/$(date +%s%N)-$$"; : > "$T1"
  take "$Q" /e/mc-remote/lock-local; rm -f "$T1"
  take "" /e/mc-remote/lock-blocal
  D="$OUT/runs/$run"; rm -rf "$D"; mkdir -p "$D"
  cp "$ROOT/tools/perf/$script.txt" "$D/script.txt"; printf '%s' "$args" > "$D/args.txt"
  echo "== run $run: $script, up to $MINUTES min, on this PC"
  powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$ROOT/tools/perf_run.ps1")" -Stage "$WO\\$STAGEDIR" -Config $CONFIG \
    -Dir "${D//\//\\}" -Minutes $MINUTES -Cache "$CACHE" > "$D/runner.txt" 2>&1
  mkdir -p "$ROOT/Saved/Perf/$run"; cp -r "$D/." "$ROOT/Saved/Perf/$run/"
  python -I "$ROOT/tools/perf_report.py" "$ROOT/Saved/Perf/$run"
  ;;
*) sed -n 2,13p "$0"; exit 2 ;;
esac
