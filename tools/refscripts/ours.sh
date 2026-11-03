#!/bin/bash
# Runs the reference timing scripts through our simulation on the remote PC and leaves the log in Saved/Remote/ours.log.
cd "$(git rev-parse --show-toplevel)" || exit 1
bash /e/mc-remote/remote.sh test McClient.McSim.RefScripts 2>&1 | tail -4
cp Saved/Remote/run_tests.log Saved/Remote/ours.log
