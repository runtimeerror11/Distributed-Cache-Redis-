# Write acknowledgement and consistency policy

## Write acknowledgement: asynchronous

A write (SET, DEL, EXPIRE, PERSIST) is acknowledged to the client **after the primary has
applied it**. The change is then streamed to the shard's replica in the background.

| | Asynchronous (current) | Synchronous (not implemented) |
|---|---|---|
| Client waits for | the primary | the primary and the replica |
| Write latency | lowest | one extra round trip |
| Replica slow or down | primary keeps serving | writes wait or fail |
| Primary crashes right after "OK" | that write can be lost | not lost |

This is the same default as Redis. It is acceptable because the cache is **not the source
of truth**: an entry may also expire or be evicted, and the application reloads it from the
database on a miss.

## Reads

Clients read from the shard's **primary**. A replica serves reads only when asked directly
(for inspection); it may lag slightly behind the primary.

## Consistency

- Each key has exactly one primary, and the primary executes commands one at a time, so
  concurrent writes to a key are serialized: **the last executed write wins**.
- With primary-only reads, a client reads its own writes, and every client sees the latest
  value the primary has applied.
- The replica is **eventually consistent**: it applies the same changes in the same order,
  shortly after the primary.
- Expiry and eviction are decided only by the primary and replicated as deletes, so the
  replica never diverges from the primary's key set.

## Ownership epoch

The cluster map carries an epoch. Every replication message carries the sender's epoch, and
a replica rejects messages from an older epoch. Today the epoch is static; with failover it
will be increased by the coordinator on every ownership change, so a stale owner cannot get
its changes applied once the others know the newer epoch.

## Failure model and limits

- Targets process crashes, not arbitrary network partitions. A timeout cannot tell a crashed
  node from a slow one, and epochs only protect nodes that have learned the newer epoch;
  full split-brain protection needs leases or fencing plus stronger coordination.
- The coordinator is a single point of failure for the control plane. Data traffic keeps
  working with the clients' cached map while it is down.
- TTL deadlines are wall-clock times; machines are assumed to have synchronized clocks.
