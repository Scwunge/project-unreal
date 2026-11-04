#!/usr/bin/env bash
# Packages the game on the 1070 (this PC does no builds): holds the build lane, syncs the project to
# C:\mc-build\<checkout>, compiles the editor (the cook needs it), cooks and stages with tools/package.ps1, and
# leaves the zip, its file list and the log under C:\mc-build\<checkout>-out on the 1070.
#
#   bash tools/package-remote.sh [yyyy-mm-dd]        run, then poll until it ends
#
# The run is detached on the 1070, so a dropped ssh does not orphan a half-built stage: run again to keep polling.
set -u
HOST=Glenn@100.106.142.98
SSH="ssh -o BatchMode=yes -o ServerAliveInterval=15 -o ServerAliveCountMax=8 -o LogLevel=ERROR -i $HOME/.ssh/desktop_d2umnj2 $HOST"
ps() { $SSH "powershell -NoProfile -Command -"; }
ROOT="$(git rev-parse --show-toplevel)"; NAME="$(basename "$ROOT")"
DATE="${1:-$(date +%F)}"
BRUN="C:/mc-build/$NAME"; OUT="C:/mc-build/$NAME-out"; WB="${BRUN//\//\\}"; WO="${OUT//\//\\}"
BLOCK=/e/mc-remote/lock-b1070

until mkdir "$BLOCK" 2>/dev/null; do
  [ -z "${bw:-}" ] && echo "1070 build lane busy ($(cat $BLOCK/owner 2>/dev/null)), waiting"; bw=1; sleep 10
done
# " build " in the owner note keeps the extra test lanes from starting beside this.
echo "$NAME build $$ (package $DATE)" > "$BLOCK/owner"; trap 'rm -rf "$BLOCK"' EXIT

cd "$ROOT"
echo "== sync $NAME -> $BRUN"
echo "\$r='$BRUN'; New-Item -ItemType Directory -Force \$r,'$OUT' | Out-Null; Remove-Item -Recurse -Force \"\$r/Source\",\"\$r/Config\",\"\$r/Content\",\"\$r/tools\" -ErrorAction SilentlyContinue" | ps
# Content: only what git tracks (the cooked game holds no Mojang files).
{ git ls-files Content; git ls-files Source Config tools; echo McClient.uproject; } | tar -cf - -T - | $SSH "tar -xf - -C $BRUN" || { echo "sync failed"; exit 1; }

cat > /tmp/run-package.ps1 <<EOF
Set-Location '$WB'
& 'C:\\UE_5.8\\Engine\\Build\\BatchFiles\\Build.bat' McClientEditor Win64 Development '-Project=$WB\\McClient.uproject' -WaitMutex -NoHotReloadFromIDE
"EDITOR_EXIT=\$LASTEXITCODE"
if (\$LASTEXITCODE -eq 0) {
  powershell -NoProfile -ExecutionPolicy Bypass -File '$WB\\tools\\package.ps1' -OutDir '$WO' -Date '$DATE'
  "PACKAGE_EXIT=\$LASTEXITCODE"
}
"RUN_DONE"
EOF
$SSH "powershell -NoProfile -Command \"\$input | Set-Content $OUT/run-package.ps1\"" < /tmp/run-package.ps1
echo "Remove-Item '$WO\\package.log' -ErrorAction SilentlyContinue; Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','$WO\\run-package.ps1' -RedirectStandardOutput '$WO\\package.log' -RedirectStandardError '$WO\\package.err'" | ps
echo "== started on the 1070, log $OUT/package.log"
while sleep 30; do
  $SSH "powershell -NoProfile -Command \"if (Test-Path $OUT/package.log) { Get-Content $OUT/package.log -Tail 3 }\"" 2>/dev/null | tail -2
  $SSH "powershell -NoProfile -Command \"Select-String -Path $OUT/package.log -Pattern RUN_DONE -Quiet\"" 2>/dev/null | grep -q True && break
done
$SSH "powershell -NoProfile -Command \"Select-String -Path $OUT/package.log -Pattern '_EXIT=|Mojang|failed|ERROR:' | Select -Last 12 | % Line\""
