# distcache: a Redis-inspired distributed in-memory cache in C++

> **Work in progress.** Single-node cache, consistent-hashing sharding with
> client-side routing, and asynchronous primary-replica replication are built
> and tested. Failure detection, replica promotion and node-join migration are
> the next stages (see [Roadmap](#roadmap)).

A sharded, replicated, in-memory key-value cache written in C++17 with no
external dependencies. Every node is a single-threaded, non-blocking event loop.

## Features

- `GET`, `SET`, `DEL`, `EXPIRE`/`PEXPIRE`, `TTL`/`PTTL`, `PERSIST` (+ `PING`, `DBSIZE`, `KEYS`, `INFO`)
- Intrusive chaining hashtable with **progressive rehashing** (no O(N) resize pause)
- **Lazy + active TTL expiry** (min-heap of deadlines, bounded work per loop iteration)
- **Exact LRU eviction** under a memory budget (`--maxmemory-mb`)
- **Consistent hashing with virtual nodes**; clients route each key straight to its shard's primary
- `MOVED` replies for keys a node does not own, so stale clients refresh their map
- **Dedicated replica per shard** with **asynchronous** replication, sequence-numbered and acknowledged
- **Full sync** when a replica (re)connects; the primary keeps serving while its replica is down
- Primary-controlled expiry and eviction: the replica copies the primary's decisions, so it stays an exact copy
- Length-prefixed binary protocol with tag-length-value responses
- Experiments: keys moved (modulo vs consistent hashing), load balance vs virtual nodes, load generator

## Architecture

```
                 +--------------------------------+
                 |   coordinator (control plane)  |
                 |   cluster map + epoch, GETMAP  |
                 +---------------+----------------+
                                 | map (at start / on MOVED or errors)
                                 v
                      +--------------------+
                      |  clients / loadgen |  hash(key) -> ring -> shard -> primary
                      +---+------------+---+
          GET/SET/DEL...  |            |   (data path goes straight to primaries)
                          v            v
                  +------------+  +------------+
                  | primary s1 |  | primary s2 |   hashtable + LRU list + TTL heap
                  +-----+------+  +-----+------+
          async repl    |               |
          (ordered,     v               v
          acked)  +------------+  +------------+
                  | replica s1 |  | replica s2 |   exact copy; read-only for clients
                  +------------+  +------------+
```

Details: [docs/design.md](docs/design.md) · [docs/consistency.md](docs/consistency.md) ·
[docs/experiments.md](docs/experiments.md)

## Project structure

```
include/distcache/
  common/     clocks, hashing, byte buffer, sockets, logging
  ds/         intrusive hashtable (progressive rehashing), doubly linked list, min-heap
  protocol/   framing, request encoding, TLV responses
  store/      Store: GET/SET/DEL/EXPIRE, lazy + active expiry, LRU eviction
  cluster/    HashRing (virtual nodes), ModuloRouter, Topology (cluster map)
  server/     EventServer (poll loop), CacheNode (primary/replica), Coordinator
  client/     blocking Connection, ClusterClient (routing + MOVED retry)
src/          implementations (same layout)
apps/         cache_node, coordinator, dcache-cli
experiments/  rehash_comparison (keys moved, balance), loadgen (throughput, hit rate)
tests/        unit tests (dependency-free runner)
scripts/      run_cluster.sh, stop_cluster.sh, smoke_test.sh (end-to-end)
config/       cluster.conf (example cluster map)
docs/         design, consistency policy, experiment results
```

## Build and test

```bash
cmake -S . -B build                 # add -DDISTCACHE_SANITIZE=ON for ASan + UBSan
cmake --build build -j
ctest --test-dir build --output-on-failure   # unit tests + live cluster smoke test
```

## Run a local cluster

`config/cluster.conf` describes 2 shards, each with a primary and a replica.

```bash
./scripts/run_cluster.sh            # coordinator on :7000, nodes on 7101/7102/7201/7202
C=./build/dcache-cli

$C set user:1 alice                 # routed by key through the cluster map
$C get user:1
$C expire user:1 60
$C ttl user:1
$C where user:1                     # which shard and primary own the key
$C map                              # the cluster map

$C --node 127.0.0.1:7101 info       # talk to one node directly
$C --node 127.0.0.1:7102 get user:1 # read from a replica (writes are rejected)

./build/loadgen --seconds 10 --keys 20000
./build/rehash_experiment

./scripts/stop_cluster.sh
```

Without a command, `dcache-cli` reads one command per line from stdin.

## Roadmap

- [x] Single node: event loop, protocol, hashtable with progressive rehashing, TTL (lazy + active), LRU
- [x] Consistent hashing with virtual nodes, client-side routing, `MOVED`, modulo comparison experiment
- [x] Primary-replica replication: async acknowledgement policy, sequence numbers, full sync on reconnect
- [ ] Heartbeats from nodes to the coordinator, failure detection
- [ ] Replica promotion with an ownership-epoch bump; old primary rejoins as replica
- [ ] Node join: controlled key migration, then publish the new map
- [ ] Failure experiments: availability and hit rate while a primary is killed

## Limitations (by design, for now)

- The coordinator is a single point of failure for the control plane (a production
  version would replicate it with a consensus protocol such as Raft).
- Asynchronous replication: a write acknowledged just before a primary crash can be lost.
  The cache is not the source of truth.
- TTL deadlines are wall-clock times so they can be copied to replicas; this assumes
  synchronized clocks.
- Memory accounting counts key + value + a fixed per-entry overhead, not process RSS.
- Custom protocol, not RESP; a ring with virtual nodes, not Redis Cluster's 16384 slots.
