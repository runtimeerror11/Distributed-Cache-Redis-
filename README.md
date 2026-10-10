# distcache — a Redis-inspired distributed cache in C++

> 🚧 **Work in progress.** The architecture and interfaces are in place; features are being
> implemented stage by stage (see [Roadmap](#roadmap)).

A sharded, replicated, in-memory key-value cache written in modern C++ (C++20).
Nodes speak the Redis protocol (RESP), so `redis-cli` and `redis-benchmark` work out of the box.

## Features (planned)
- `GET`, `SET`, `DEL`, `EXPIRE`, `TTL` over RESP
- **Consistent hashing with virtual nodes** for sharding
- **Primary + replica** per key with a configurable write-acknowledgement policy
- **Lazy + periodic TTL expiry**
- **Bounded-memory LRU eviction**
- **Heartbeat failure detection** and replica promotion using an **ownership epoch**
- **Rebalancing**: only affected keys move when a node joins
- Experiments: keys moved (modulo vs consistent hashing), hit rate and availability during failures

## Architecture
```
            redis-cli / benchmark
                     │  RESP over TCP
     ┌───────────────┼───────────────┐
     ▼               ▼               ▼
 cache_node-1   cache_node-2   cache_node-3      (each: KvStore + LRU + TTL,
     ▲  replication  ▲  replication  ▲            primary for some ranges,
     └───────────────┴───────────────┘            replica for others)
                     ▲ heartbeats / cluster view (epoch)
                coordinator
```
Details: [docs/design.md](docs/design.md) · [docs/consistency.md](docs/consistency.md)

## Project structure
```
include/distcache/   public headers
  store/             KvStore (map + TTL), LruPolicy
  protocol/          RESP parser/encoder
  cluster/           HashRing, Membership (failure detector), Ownership (epochs), Rebalancer
  replication/       Replicator + ack policy
  server/            TcpServer, CommandHandler
src/                 implementations (same layout)
apps/                cache_node, coordinator executables
experiments/         rehash comparison, failure/availability measurement
tests/               GoogleTest unit tests
scripts/             run_cluster.sh, kill_node.sh, plot_results.py
docs/                design, consistency policy, experiment results
```

## Build
```bash
cmake -S . -B build            # add -DDISTCACHE_SANITIZE=ON while developing
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Run
```bash
./scripts/run_cluster.sh 3
redis-cli -p 7001 SET user:1 alice
redis-cli -p 7001 EXPIRE user:1 60
redis-cli -p 7001 GET user:1
```

## Roadmap
- [x] Project structure, interfaces, build, CI
- [ ] Stage 1 — single node: KvStore, TTL (lazy + periodic), LRU, RESP, TCP server
- [ ] Stage 2 — consistent hashing + virtual nodes, modulo comparison experiment
- [ ] Stage 3 — primary/replica replication, ack policy
- [ ] Stage 4 — heartbeats, failure detection, epoch-based failover
- [ ] Stage 5 — node join and key transfer
- [ ] Stage 6 — experiments, benchmarks, plots

## Results
_Coming soon: keys moved, failover time, availability and hit rate during failures, throughput._
