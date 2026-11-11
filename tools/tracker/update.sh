#!/usr/bin/env bash
# Reruns the content audit and copies progress.json (and, optionally, the widget files) to an output folder.
# Uploads nothing: point OUT_DIR at a folder your web host serves or syncs.
#
#   tools/tracker/update.sh [OUT_DIR]
#
# Settings (environment variables):
#   OUT_DIR        where progress.json is copied (default: tools/tracker/out). The first argument wins over this.
#   AUDIT_REF      git revision to audit (default: m1/blocks-and-chunks; set to "" for the working tree)
#   TEST_LOG       an Unreal automation log of a run of that revision (optional; without one nothing is "verified")
#   TEST_LOG_NOTE  free text stored with the log, e.g. "run_tests.ps1 on 31df2ee"
#   COPY_WIDGET    1 = also copy tracker.js, tracker.css and index.html to OUT_DIR (default 0)
#   PYTHON         python executable (default: python3, else python)
#
# The history in OUT_DIR/progress.json is kept: the audit reads it and appends this run.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT_DIR="${1:-${OUT_DIR:-$HERE/out}}"
AUDIT_REF="${AUDIT_REF-m1/blocks-and-chunks}"
COPY_WIDGET="${COPY_WIDGET:-0}"

if [ -z "${PYTHON:-}" ]; then
    if command -v python3 >/dev/null 2>&1 && python3 -c 'import sys' >/dev/null 2>&1; then PYTHON=python3; else PYTHON=python; fi
fi

mkdir -p "$OUT_DIR"
TARGET="$OUT_DIR/progress.json"

args=(--repo "$REPO" --out "$TARGET")
[ -n "$AUDIT_REF" ] && args+=(--ref "$AUDIT_REF")
[ -n "${TEST_LOG:-}" ] && args+=(--test-log "$TEST_LOG")
[ -n "${TEST_LOG_NOTE:-}" ] && args+=(--test-log-note "$TEST_LOG_NOTE")

# Writes to a temp file and renames it, so a web server never serves a half-written progress.json.
"$PYTHON" "$HERE/audit.py" "${args[@]}"

if [ "$COPY_WIDGET" = "1" ]; then
    for f in tracker.js tracker.css index.html; do
        cp "$HERE/widget/$f" "$OUT_DIR/$f.tmp" && mv -f "$OUT_DIR/$f.tmp" "$OUT_DIR/$f"
    done
    echo "copied the widget files to $OUT_DIR"
fi
