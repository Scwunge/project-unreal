#!/usr/bin/env bash
# The playability harness, run on the 1070 from this PC. Builds the packaged game there and runs scripted sessions of it.
#
#   bash tools/perf-remote.sh build [full]            compile Shipping (and the editor plus a cook with "full" or when no
#                                                     stage exists) into C:\mc-build\<checkout>-out\stage
#   bash tools/perf-remote.sh run <name> <script> [game args]
#                                                     play tools/perf/<script>.txt in the stage as <name>, on the 1070's
#                                                     desktop, and bring the record to Saved/Perf/<name>/
#   bash tools/perf-remote.sh soak <name> [rounds]    the soak test: a new world through tools/perf/soak_round.txt (3 rounds,
#                                                     about 30 minutes), then a new process reopens it. Exit 1 on any crash,
#                                                     with the stacks named. Shares the card (MC_PERF_SHARED) and not the build lane.
#   bash tools/perf-remote.sh report <name>           prints a finished run's summary
#   bash tools/perf-remote.sh symbols <name>          names the frames of a run's stacks
#
# A run takes its turn in the desktop queue, so no vanilla client or shot shares the card, and notes what else was running.
# MC_PERF_ALONE=1 also holds the 1070's test lanes, or exits 3 if they stay busy. MC_PERF_MINUTES sets a run's time limit (default 8). Runs of up to MC_PERF_HOLD_MINUTES (default 10) also hold the
# build lane like a shot does, since a build beside a run spoils the numbers; longer runs (a soak) don't.
set -u
HOST=Glenn@100.106.142.98
SSH="ssh -o BatchMode=yes -o ServerAliveInterval=15 -o ServerAliveCountMax=8 -o LogLevel=ERROR -i $HOME/.ssh/desktop_d2umnj2 $HOST"
ps() { $SSH "powershell -NoProfile -Command -"; }
ROOT="$(git rev-parse --show-toplevel)"; NAME="$(basename "$ROOT")"
BRUN="C:/mc-build/$NAME"; OUT="C:/mc-build/$NAME-out"; WB="${BRUN//\//\\}"; WO="${OUT//\//\\}"
mode="${1:-}"; shift || true
BLOCK=/e/mc-remote/lock-b1070

case "$mode" in
build)
  until mkdir "$BLOCK" 2>/dev/null; do
    [ -z "${bw:-}" ] && echo "1070 build lane busy ($(cat $BLOCK/owner 2>/dev/null)), waiting"; bw=1; sleep 10
  done
  echo "$NAME build $$ (perf)" > "$BLOCK/owner"; trap 'rm -rf "$BLOCK"' EXIT
  cd "$ROOT"
  echo "== sync $NAME -> $BRUN"
  echo "\$r='$BRUN'; New-Item -ItemType Directory -Force \$r,'$OUT' | Out-Null; Remove-Item -Recurse -Force \"\$r/Source\",\"\$r/Config\",\"\$r/Content\",\"\$r/tools\" -ErrorAction SilentlyContinue" | ps
  { git ls-files Content tools Config; git ls-files -co --exclude-standard Source; echo McClient.uproject; } | tar -cf - -T - | $SSH "tar -xf - -C $BRUN" || { echo "sync failed"; exit 1; }
  FULL=0; [ "${1:-}" = full ] && FULL=1
  mkdir -p "$ROOT/Saved"
  cat > "$ROOT/Saved/perf-build.ps1" <<EOF
\$ErrorActionPreference = 'Continue'
\$Stage = '$WO\\stage'
Set-Location '$WB'
\$Uat = 'C:\\UE_5.8\\Engine\\Build\\BatchFiles\\RunUAT.bat'
\$Common = @('BuildCookRun', '-project=$WB\\McClient.uproject', '-noP4', '-platform=Win64', '-clientconfig=Shipping', '-target=McClient', '-build', '-stage', '-pak', '-archive', "-archivedirectory=\$Stage", '-utf8output', '-nocompileeditor')
if ($FULL -eq 1 -or -not (Test-Path "\$Stage\\Windows\\McClient\\Content\\Paks")) {
  & 'C:\\UE_5.8\\Engine\\Build\\BatchFiles\\Build.bat' McClientEditor Win64 Development '-Project=$WB\\McClient.uproject' -WaitMutex -NoHotReloadFromIDE
  "EDITOR_EXIT=\$LASTEXITCODE"
  if (\$LASTEXITCODE -ne 0) { "RUN_DONE"; exit 1 }
  & \$Uat @Common -cook
} else {
  # Code only: compile Shipping and put the new executable in the stage; the cooked content stays.
  & 'C:\\UE_5.8\\Engine\\Build\\BatchFiles\\Build.bat' McClient Win64 Shipping '-Project=$WB\\McClient.uproject' -WaitMutex
  if (\$LASTEXITCODE -eq 0) { Copy-Item '$WB\\Binaries\\Win64\\McClient-Win64-Shipping.exe' "\$Stage\\Windows\\McClient\\Binaries\\Win64\\" -Force }
}
"BUILD_EXIT=\$LASTEXITCODE"
"RUN_DONE"
EOF
  mkdir -p "$ROOT/Saved"; $SSH "powershell -NoProfile -Command \"\$input | Set-Content $OUT/perf-build.ps1\"" < "$ROOT/Saved/perf-build.ps1"
  echo "== building on the 1070 (holding this connection), log $OUT/perf-build.log"
  $SSH "powershell -NoProfile -ExecutionPolicy Bypass -File $WO\\perf-build.ps1 > $WO\\perf-build.log 2>&1"
  $SSH "powershell -NoProfile -Command \"Select-String -Path $OUT/perf-build.log -Pattern '_EXIT=|error C|error LNK|: error |BUILD SUCCESSFUL|Total execution time' | Select -Last 30 | % Line\"" | tr -d '\r' | tee "$ROOT/Saved/perf-build.out"
  grep -q "BUILD_EXIT=0" "$ROOT/Saved/perf-build.out"
  ;;
run)
  run="$1"; script="$2"; shift 2; args="$*"
  [ -f "$ROOT/tools/perf/$script.txt" ] || { echo "no tools/perf/$script.txt"; exit 2; }
  MINUTES="${MC_PERF_MINUTES:-8}"
  # The desktop, first come first served with ref.sh and shots, then the shot lane and its task.
  Q=/e/mc-remote/queue; SQ=/e/mc-remote/queue-shot; mkdir -p "$Q" "$SQ"
  T1="$Q/$(date +%s%N)-$$"; T2="$SQ/$(date +%s%N)-$$"; : > "$T1"; : > "$T2"
  turn() { local t p; for t in $(ls "$1" 2>/dev/null | sort); do p="${t##*-}"; if command ps -p "$p" >/dev/null 2>&1; then [ "$p" = "$$" ]; return; fi; rm -f "$1/$t"; done; return 0; }
  held=""; cleanup() { rm -f "$T1" "$T2"; for l in $held; do rm -rf "$l"; done; }; trap cleanup EXIT
  take() { # queue dir, lock
    until { [ -z "$1" ] || turn "$1"; } && mkdir "$2" 2>/dev/null; do [ -z "${said:-}" ] && echo "waiting for $(basename "$2") ($(cat "$2/owner" 2>/dev/null))"; said=1; sleep 10; done
    said=; echo "$NAME perf $$" > "$2/owner"; held="$held $2"
  }
  if [ "${MC_PERF_SHARED:-0}" = 1 ]; then rm -f "$T1"; else take "$Q" /e/mc-remote/lock; rm -f "$T1"; fi
  take "$SQ" /e/mc-remote/lock-s1070; rm -f "$T2"
  [ "$MINUTES" -le "${MC_PERF_HOLD_MINUTES:-10}" ] && take "" "$BLOCK"
  # MC_PERF_ALONE=1: the 1070's test lanes too, for frame numbers with nothing beside them. A long test in one of them would
  # leave every lane above idle, so this gives up after two minutes (exit 3, everything released); try again later.
  # A chunk hash doesn't depend on timing, so it never asks.
  if [ "${MC_PERF_ALONE:-0}" = 1 ] && [ "$script" != chunkhash ]; then
    for l in d c b ""; do
      lane="/e/mc-remote/lock-t1070$l"; waited=0
      until mkdir "$lane" 2>/dev/null; do
        waited=$((waited + 10))
        [ "$waited" -gt 120 ] && { echo "RUN_NOT_ALONE: $(basename "$lane") busy ($(cat "$lane/owner" 2>/dev/null)), gave up"; exit 3; }
        sleep 10
      done
      echo "$NAME perf $$" > "$lane/owner"; held="$held $lane"
    done
    # And nothing else working the machine, whoever's it is. Looks twice, since a single reading can catch a passing spike.
    waited=0
    while :; do
      busy="$(echo "\$c=(Get-CimInstance Win32_Processor).LoadPercentage; \$g=(& nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits).Trim(); Start-Sleep 8; \$c2=(Get-CimInstance Win32_Processor).LoadPercentage; \$g2=(& nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits).Trim(); if ([math]::Min(\$c,\$c2) -gt 25 -or [math]::Min([int]\$g,[int]\$g2) -gt 25 -or (Get-Process UnrealEditor-Cmd -ErrorAction SilentlyContinue)) { \"cpu \$c \$c2 gpu \$g \${g2}: \" + ((Get-Process | Sort-Object CPU -Descending | Select-Object -First 4 | % Name) -join ' ') }" | ps | tr -d '\r')"
      [ -z "$busy" ] && break
      waited=$((waited + 18))
      [ "$waited" -gt 120 ] && { echo "RUN_NOT_ALONE: machine busy, $busy"; exit 3; }
      sleep 10
    done
  fi
  echo "== run $run: $script, up to $MINUTES min"
  $SSH "powershell -NoProfile -Command \"New-Item -ItemType Directory -Force $OUT/runs/$run | Out-Null; \$input | Set-Content $OUT/runs/$run/script.txt\"" < "$ROOT/tools/perf/$script.txt"
  printf '%s' "$args" | $SSH "powershell -NoProfile -Command \"\$input | Set-Content $OUT/runs/$run/args.txt\""
  $SSH "powershell -NoProfile -Command \"\$input | Set-Content $OUT/perf_run.ps1\"" < "$ROOT/tools/perf_run.ps1"
  printf '%s\r\n' "powershell -NoProfile -ExecutionPolicy Bypass -File $WO\\perf_run.ps1 -Stage $WO\\stage -Dir $WO\\runs\\$run -Minutes $MINUTES > $WO\\runs\\$run\\runner.txt 2>&1" "echo done> $WO\\runs\\$run\\done.txt" > "$ROOT/Saved/perf-job.cmd"
  $SSH "powershell -NoProfile -Command \"\$input | Set-Content C:/mc-runs/job2.cmd\"" < "$ROOT/Saved/perf-job.cmd"
  $SSH "schtasks /run /tn McRemoteJob2" >/dev/null || { echo "could not start McRemoteJob2"; exit 1; }
  deadline=$((SECONDS + MINUTES * 60 + 180))
  while [ "$SECONDS" -lt "$deadline" ]; do
    sleep 15
    $SSH "if exist \"$WO\\runs\\$run\\done.txt\" echo yes" 2>/dev/null | grep -q yes && break
  done
  mkdir -p "$ROOT/Saved/Perf/$run"
  $SSH "tar -cf - -C $OUT/runs/$run ." | tar -xf - -C "$ROOT/Saved/Perf/$run"
  python -I "$ROOT/tools/perf_report.py" "$ROOT/Saved/Perf/$run"
  ;;
soak)
  run="$1"; rounds="${2:-3}"; world="soak$(date +%m%d%H%M)"
  s="$ROOT/tools/perf/soak_generated.txt"
  { cat "$ROOT/tools/perf/soak_head.txt"; for _ in $(seq 1 "$rounds"); do cat "$ROOT/tools/perf/soak_round.txt"; done; cat "$ROOT/tools/perf/soak_tail.txt"; } > "$s"
  export MC_PERF_SHARED=1 MC_PERF_MINUTES=$((rounds * 15 + 10))
  bash "$0" run "$run" soak_generated -McWorld=$world -McSeed=12 > "$ROOT/Saved/perf-soak.out" 2>&1; a=$?
  tail -60 "$ROOT/Saved/perf-soak.out"
  MC_PERF_MINUTES=6 bash "$0" run "$run-reload" soak_reload -McWorld=$world; b=$?
  rm -f "$s"
  [ $a = 0 ] || bash "$0" symbols "$run"
  [ $b = 0 ] || bash "$0" symbols "$run-reload"
  echo "SOAK: first half $([ $a = 0 ] && echo clean || echo FAILED), reload $([ $b = 0 ] && echo clean || echo FAILED)"
  [ $a = 0 ] && [ $b = 0 ]
  ;;
symbols)
  $SSH "powershell -NoProfile -Command \"\$input | Set-Content $OUT/perf_symbols.ps1\"" < "$ROOT/tools/perf_symbols.ps1"
  $SSH "powershell -NoProfile -ExecutionPolicy Bypass -File $WO\perf_symbols.ps1 -Log $WO\runs\\$1\perf.log -Exe $WB\Binaries\Win64\McClient-Win64-Shipping.exe"
  ;;
report) python -I "$ROOT/tools/perf_report.py" "$ROOT/Saved/Perf/$1" ;;
*) sed -n 2,15p "$0"; exit 2 ;;
esac
