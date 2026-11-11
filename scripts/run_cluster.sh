#!/usr/bin/env bash
# Starts a local cluster from a map file: the coordinator plus, for every shard,
# its primary and (if listed) its replica.
#
# Usage: ./scripts/run_cluster.sh [config] [build_dir] [maxmemory_mb]
#   defaults: config/cluster.conf, build, 64
# Logs and pid files go to ./logs. Stop with ./scripts/stop_cluster.sh
set -euo pipefail

CONFIG=${1:-config/cluster.conf}
BIN=${2:-build}
MAXMEM=${3:-64}
COORD_PORT=${COORD_PORT:-7000}
LOGS=${LOGS:-logs}

for b in coordinator cache_node; do
  [[ -x "$BIN/$b" ]] || { echo "missing $BIN/$b (build first: cmake -S . -B build && cmake --build build -j)"; exit 1; }
done
mkdir -p "$LOGS"

start() {  # start <name> <command...>
  local name=$1; shift
  "$@" >"$LOGS/$name.log" 2>&1 &
  echo $! >"$LOGS/$name.pid"
  echo "started $name (pid $!)"
}

start coordinator "$BIN/coordinator" --config "$CONFIG" --port "$COORD_PORT"

# Lines look like: shard <id> <primary> [replica]
while read -r word id primary replica; do
  [[ "$word" == "shard" ]] || continue
  start "$id-primary" "$BIN/cache_node" --shard "$id" --role primary \
    --coord "127.0.0.1:$COORD_PORT" --maxmemory-mb "$MAXMEM"
  if [[ -n "${replica:-}" && "${replica:0:1}" != "#" ]]; then
    start "$id-replica" "$BIN/cache_node" --shard "$id" --role replica \
      --coord "127.0.0.1:$COORD_PORT" --maxmemory-mb "$MAXMEM"
  fi
done <"$CONFIG"

echo
echo "Try:  $BIN/dcache-cli --coord 127.0.0.1:$COORD_PORT set user:1 alice"
echo "      $BIN/dcache-cli --coord 127.0.0.1:$COORD_PORT get user:1"
echo "      $BIN/dcache-cli --coord 127.0.0.1:$COORD_PORT where user:1"
