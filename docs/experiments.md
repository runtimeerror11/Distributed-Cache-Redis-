# Experiments

Measured on a Linux development container (single machine, all processes on localhost),
release build. Rerun them on your own machine before quoting numbers.

## 1. Keys moved: modulo vs consistent hashing

`./build/rehash_experiment` (100,000 keys, 100 virtual nodes per shard)

| Nodes | Modulo `hash % N` | Consistent hashing | Ideal 1/(N+1) |
|---|---|---|---|
| 2 -> 3 | 66.4% | 39.3% | 33.3% |
| 3 -> 4 | 74.9% | 22.2% | 25.0% |
| 4 -> 5 | 80.0% | 17.5% | 20.0% |
| 5 -> 6 | 83.3% | 13.8% | 16.7% |
| 6 -> 7 | 85.6% | 14.5% | 14.3% |
| 7 -> 8 | 87.5% | 15.5% | 12.5% |
| 8 -> 9 | 88.7% | 10.5% | 11.1% |
| 9 -> 10 | 89.9% | 10.1% | 10.0% |

Modulo hashing moves about N/(N+1) of all keys when one node is added; consistent hashing
moves about 1/(N+1), and only to the new node. Removing one of 5 nodes moved 26.9% of keys,
all of them keys that lived on the removed node.

## 2. Load balance vs virtual nodes

5 nodes, 100,000 keys, share of keys per node (ideal 20%):

| Virtual nodes | Min | Max | Std dev | Max / ideal |
|---|---|---|---|---|
| 1 | 1.8% | 59.5% | 21.0% | 2.97x |
| 10 | 9.5% | 29.7% | 7.6% | 1.48x |
| 50 | 17.3% | 25.7% | 3.1% | 1.28x |
| 100 | 17.5% | 26.9% | 3.6% | 1.34x |
| 200 | 18.9% | 21.2% | 0.8% | 1.06x |
| 500 | 18.9% | 21.0% | 0.8% | 1.05x |

With one point per node the busiest node holds almost 3x its fair share; a few hundred
points per node bring every node within a few percent of ideal. `vnodes` in the cluster map
sets this.

## 3. Throughput and latency (no failures)

`./build/loadgen --seconds 4 --keys 20000` against 2 shards (primary + replica each),
one synchronous client, 90% reads, Zipf(0.99) key popularity, 32-byte values:

| Metric | Result |
|---|---|
| Throughput | ~26,000 requests/s (one client, one request in flight) |
| Availability | 100% |
| Hit rate | 100% (all keys preloaded, no eviction or expiry) |
| Latency | p50 36 us, p99 77 us |
| Replication lag on the primary | 1 operation (`INFO repl_lag_ops`) |

## 4. Hit rate and availability during failures

_Next stage: run `loadgen` while killing a primary, once heartbeat failure detection and
replica promotion are implemented._
