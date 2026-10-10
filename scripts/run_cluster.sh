#!/usr/bin/env bash
# Starts a local cluster: 1 coordinator + N cache nodes. Usage: ./scripts/run_cluster.sh [N]
set -euo pipefail
N=${1:-3}
BIN=./build
mkdir -p logs
$BIN/coordinator --port 7000 > logs/coordinator.log 2>&1 &
for i in $(seq 1 "$N"); do
  $BIN/cache_node --id "node-$i" --port $((7000 + i)) > "logs/node-$i.log" 2>&1 &
done
echo "Started coordinator + $N nodes. Try: redis-cli -p 7001 PING"
