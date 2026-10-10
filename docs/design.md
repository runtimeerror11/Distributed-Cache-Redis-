# Design

## Components
- **cache_node** — stores a shard of keys in memory (`KvStore`), speaks RESP over TCP,
  acts as primary for some key ranges and replica for others.
- **coordinator** — receives heartbeats, detects failures, promotes replicas,
  and publishes a new `ClusterView` with an incremented **epoch**.

## Key routing
Consistent hashing with virtual nodes (`HashRing`). For a key, `preference_list(key, 2)`
gives `[primary, replica]`. A node that does not own a key replies `-MOVED <node> <addr>`.

## Expiry
- **Lazy:** every GET/TTL checks `expires_at` and deletes expired keys.
- **Periodic:** a background thread samples keys with a TTL every 100 ms and deletes expired ones.

## Eviction
Bounded by `--max-memory-mb`. When exceeded, evict the least recently used key (O(1) LRU).

## Failure detection and failover
Heartbeat every 500 ms. Suspect after 1 s, Dead after 3 s. On Dead the coordinator promotes
the replica, increments the epoch and broadcasts the view. Requests carrying an older epoch
are rejected, so a recovered old primary cannot accept writes.

## Node join (rebalancing)
New node registers → coordinator bumps epoch → each node computes
`Rebalancer::keys_to_transfer` and streams only those keys to the new owner.

_TODO: add architecture diagram (docs/architecture.png)._
