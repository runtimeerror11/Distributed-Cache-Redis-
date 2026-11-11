#!/usr/bin/env bash
# Stops every process started by run_cluster.sh (graceful SIGTERM).
# Usage: ./scripts/stop_cluster.sh
set -uo pipefail
LOGS=${LOGS:-logs}
shopt -s nullglob
for f in "$LOGS"/*.pid; do
  pid=$(cat "$f")
  if kill "$pid" 2>/dev/null; then echo "stopped $(basename "$f" .pid) (pid $pid)"; fi
  rm -f "$f"
done
