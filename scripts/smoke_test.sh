#!/usr/bin/env bash
# End-to-end test on a real local cluster: 1 coordinator + 2 shards, each with a
# primary and a replica. Covers the single-node commands, TTL, LRU eviction,
# consistent-hashing routing, MOVED, replication, and full sync after a replica
# restart. (Failover/promotion is the next stage and is not tested here.)
#
# Usage: scripts/smoke_test.sh <build_dir>     (ctest runs it automatically)
set -uo pipefail

BIN=$(cd "${1:-build}" && pwd)
CLI="$BIN/dcache-cli"
TMP=$(mktemp -d)
BASE=${SMOKE_BASE_PORT:-17000}
COORD="127.0.0.1:$BASE"
S1P="127.0.0.1:$((BASE + 101))"; S1R="127.0.0.1:$((BASE + 102))"
S2P="127.0.0.1:$((BASE + 201))"; S2R="127.0.0.1:$((BASE + 202))"
PASS=0; FAIL=0
declare -A PIDS

cat >"$TMP/cluster.conf" <<EOF
epoch 1
vnodes 100
shard s1 $S1P $S1R
shard s2 $S2P $S2R
EOF

cleanup() {
  for name in "${!PIDS[@]}"; do kill -9 "${PIDS[$name]}" 2>/dev/null; done
  rm -rf "$TMP"
}
trap cleanup EXIT

ok()   { PASS=$((PASS + 1)); echo "  ok   $1"; }
bad()  { FAIL=$((FAIL + 1)); echo "  FAIL $1"; echo "       got: $2"; }
expect() {  # expect <description> <expected substring> <command...>
  local desc=$1 want=$2; shift 2
  local got; got=$("$@" 2>&1)
  if [[ "$got" == *"$want"* ]]; then ok "$desc"; else bad "$desc" "$got"; fi
}
info_field() { "$CLI" --node "$1" info | sed -n "s/^$2://p"; }
wait_until() {  # wait_until <seconds> <command...>: retry until the command succeeds
  local deadline=$((SECONDS + $1)); shift
  until "$@" >/dev/null 2>&1; do
    (( SECONDS >= deadline )) && return 1
    sleep 0.1
  done
}
start() {  # start <name> <args...>
  local name=$1; shift
  "$@" >"$TMP/$name.log" 2>&1 &
  PIDS[$name]=$!
}
node() {  # node <shard> <role>
  start "$1-$2" "$BIN/cache_node" --shard "$1" --role "$2" --coord "$COORD" --maxmemory-mb 1
}

echo "== starting cluster in $TMP"
start coordinator "$BIN/coordinator" --config "$TMP/cluster.conf" --port "$BASE"
node s1 primary; node s1 replica; node s2 primary; node s2 replica
for a in "$COORD" "$S1P" "$S1R" "$S2P" "$S2R"; do
  wait_until 10 "$CLI" --node "$a" ping || { echo "node $a did not start"; cat "$TMP"/*.log; exit 1; }
done
wait_until 10 bash -c "'$CLI' --node $S1P info | grep -q 'repl_link:online'"
wait_until 10 bash -c "'$CLI' --node $S2P info | grep -q 'repl_link:online'"

echo "== single node commands"
expect "ping" '"PONG"' "$CLI" --node "$S1P" ping
expect "map has 2 shards" "shard s2" "$CLI" --coord "$COORD" map
expect "set" '"OK"' "$CLI" --coord "$COORD" set user:1 alice
expect "get" '"alice"' "$CLI" --coord "$COORD" get user:1
expect "overwrite" '"OK"' "$CLI" --coord "$COORD" set user:1 bob
expect "last write wins" '"bob"' "$CLI" --coord "$COORD" get user:1
expect "get missing" "(nil)" "$CLI" --coord "$COORD" get no:such:key
expect "ttl without expiry" "(int) -1" "$CLI" --coord "$COORD" ttl user:1
expect "ttl missing key" "(int) -2" "$CLI" --coord "$COORD" ttl no:such:key
expect "del" "(int) 1" "$CLI" --coord "$COORD" del user:1
expect "del again" "(int) 0" "$CLI" --coord "$COORD" del user:1
expect "unknown command" "unknown command" "$CLI" --node "$S1P" bogus
expect "wrong arity" "wrong number of arguments" "$CLI" --node "$S1P" get
expect "bad ttl" "not an integer" "$CLI" --coord "$COORD" expire user:1 abc

echo "== TTL: lazy and active expiry"
"$CLI" --coord "$COORD" set temp:1 v >/dev/null
expect "expire" "(int) 1" "$CLI" --coord "$COORD" expire temp:1 1
expect "ttl counts down" "(int) 1" "$CLI" --coord "$COORD" ttl temp:1
sleep 1.3
expect "expired key is gone (lazy)" "(nil)" "$CLI" --coord "$COORD" get temp:1
for i in $(seq 1 40); do echo "set short:$i v"; echo "pexpire short:$i 200"; done \
  | "$CLI" --coord "$COORD" >/dev/null
sleep 1
expired=$(( $(info_field "$S1P" expired) + $(info_field "$S2P" expired) ))
if (( expired >= 40 )); then ok "never-read keys removed by active expiry ($expired)"; \
else bad "active expiry" "expired=$expired"; fi

echo "== consistent hashing: routing and MOVED"
for i in $(seq 1 200); do echo "set k:$i value-$i"; done | "$CLI" --coord "$COORD" >/dev/null
n1=$("$CLI" --node "$S1P" dbsize | awk '{print $2}')
n2=$("$CLI" --node "$S2P" dbsize | awk '{print $2}')
if (( n1 > 50 && n2 > 50 && n1 + n2 == 200 )); then ok "200 keys split across shards ($n1 + $n2)"; \
else bad "keys split across shards" "s1=$n1 s2=$n2"; fi
wrong=""
for i in $(seq 1 200); do
  if "$CLI" --coord "$COORD" where "k:$i" | grep -q "shard s2"; then wrong="k:$i"; break; fi
done
expect "key of s2 sent to s1 gets MOVED" "MOVED s2 $S2P" "$CLI" --node "$S1P" get "$wrong"
expect "client follows the map to the right shard" '"value-' "$CLI" --coord "$COORD" get "$wrong"
all_ok=1
for i in $(seq 1 200); do echo "get k:$i"; done | "$CLI" --coord "$COORD" >"$TMP/gets.txt"
for i in $(seq 1 200); do grep -qx "\"value-$i\"" "$TMP/gets.txt" || all_ok=0; done
if (( all_ok )); then ok "all 200 keys read back through routing"; else bad "read back" "missing values"; fi

echo "== replication (async, primary -> dedicated replica)"
s1key=$(for i in $(seq 1 200); do "$CLI" --coord "$COORD" where "k:$i" | grep -q "shard s1" && { echo "k:$i"; break; }; done)
wait_until 5 bash -c "'$CLI' --node $S1R get $s1key | grep -q value"
expect "replica has the primary's key" '"value-' "$CLI" --node "$S1R" get "$s1key"
expect "replica rejects client writes" "READONLY" "$CLI" --node "$S1R" set "$s1key" x
"$CLI" --coord "$COORD" del "$s1key" >/dev/null
wait_until 5 bash -c "'$CLI' --node $S1R get $s1key | grep -q nil"
expect "delete replicated" "(nil)" "$CLI" --node "$S1R" get "$s1key"
for pair in "$S1P $S1R" "$S2P $S2R"; do
  read -r pri rep <<<"$pair"
  wait_until 5 bash -c "[[ \$('$CLI' --node $pri dbsize) == \$('$CLI' --node $rep dbsize) ]]"
  expect "replica $rep key count matches primary" "$("$CLI" --node "$pri" dbsize)" "$CLI" --node "$rep" dbsize
done

echo "== LRU eviction under a 1 MiB budget (primary decides, replica copies)"
big=$(head -c 8000 /dev/zero | tr '\0' 'x')
for i in $(seq 1 400); do echo "set big:$i $big"; done | "$CLI" --coord "$COORD" >/dev/null
ev=$(( $(info_field "$S1P" evictions) + $(info_field "$S2P" evictions) ))
if (( ev > 0 )); then ok "evictions happened ($ev)"; else bad "evictions" "0"; fi
used=$(info_field "$S1P" used_bytes); max=$(info_field "$S1P" max_bytes)
if (( used <= max )); then ok "s1 stays within budget ($used <= $max)"; else bad "budget" "$used > $max"; fi
expect "most recent key survives" '"xxx' "$CLI" --coord "$COORD" get big:400
wait_until 5 bash -c "[[ \$('$CLI' --node $S1P dbsize) == \$('$CLI' --node $S1R dbsize) ]]"
expect "replica evicted the same keys" "$("$CLI" --node "$S1P" dbsize)" "$CLI" --node "$S1R" dbsize

echo "== replica crash and full sync"
{ kill -9 "${PIDS[s1-replica]}"; wait "${PIDS[s1-replica]}"; } 2>/dev/null
unset 'PIDS[s1-replica]'
wait_until 5 bash -c "'$CLI' --node $S1P info | grep -q 'repl_link:disconnected'"
expect "primary notices the replica is gone" "repl_link:disconnected" "$CLI" --node "$S1P" info
for i in $(seq 1 50); do echo "set after:$i v$i"; done | "$CLI" --coord "$COORD" >"$TMP/after.txt"
if [[ $(grep -c '"OK"' "$TMP/after.txt") == 50 ]]; then ok "writes keep working without the replica"; \
else bad "writes without replica" "$(sort "$TMP/after.txt" | uniq -c)"; fi
node s1 replica
wait_until 10 bash -c "'$CLI' --node $S1P info | grep -q 'repl_link:online'"
expect "link back online" "repl_link:online" "$CLI" --node "$S1P" info
syncs=$(info_field "$S1P" full_syncs)
if (( syncs >= 2 )); then ok "full sync after reconnect (full_syncs=$syncs)"; else bad "full sync" "$syncs"; fi
expect "restarted replica has every key" "$("$CLI" --node "$S1P" dbsize)" "$CLI" --node "$S1R" dbsize

echo "== clean shutdown"
for name in "${!PIDS[@]}"; do kill -TERM "${PIDS[$name]}" 2>/dev/null; done
for name in "${!PIDS[@]}"; do
  if wait "${PIDS[$name]}"; then ok "$name exited cleanly"; else bad "$name exit" "status $?"; cat "$TMP/$name.log"; fi
  unset "PIDS[$name]"
done

echo
echo "passed $PASS, failed $FAIL"
(( FAIL == 0 ))
