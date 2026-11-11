# Design

## Components and paths

| Path | Between | Carries |
|---|---|---|
| Data | client -> shard primary | GET, SET, DEL, EXPIRE, TTL, PERSIST |
| Replication | primary -> its replica | every change the primary makes, in order |
| Control | client/node <-> coordinator | the cluster map (`GETMAP`) |

- **cache_node**: the primary or the replica of one shard. Started with `--shard <id> --role
  primary|replica`; it loads the map, finds its own address in it and listens there.
- **coordinator**: owns the cluster map (shards, primary and replica addresses, ownership
  epoch, virtual nodes per shard). It is never on the data path.
- **dcache-cli / loadgen**: clients that route by key using the same ring code as the nodes.

## One node

### Event loop (`EventServer`)
- One thread, non-blocking sockets, `poll()`. Every command runs to completion before the
  next, so the store needs no locks and each command is atomic.
- Each connection has an input and an output buffer. TCP has no message boundaries, so a
  request is processed only once all its bytes have arrived; several requests in one read
  are all processed (pipelining). Replies are written straight into the output buffer.
- After processing, the server tries to write immediately instead of waiting for the next
  `poll()`. Unsent bytes stay buffered; while replies are pending the connection is not read
  (backpressure).
- Timers: `poll()`'s timeout is the nearest deadline (idle connections, key expiry,
  replication reconnect/ping). Idle client connections are kept in a list ordered by last
  activity (moved to the back on every event), so only the front can be due.
- The primary's connection to its replica is an outbound connection in the same loop, so
  replication never blocks client requests.

### Protocol
```
message  = [u32 body_len][body]
request  = [u32 nstr]([u32 len][bytes])...          list of strings
response = one TLV value: nil | err(code,msg) | str | int | dbl | arr(n values)
```
Little-endian integers; bodies are capped at 32 MiB. The response length is unknown until
it is written, so 4 bytes are reserved first and patched afterwards.

### Store
- `Entry` embeds three links: a hashtable node, an LRU list node and a heap index. One
  allocation per key; entries never move, so the list and the heap can point into them.
- **Hashtable**: chaining, power-of-two size (`hash & mask`), max load factor 8. Growth is
  **progressive**: the old and new tables coexist and each operation moves up to 128 keys,
  so a large table never pauses the server for an O(N) resize. Lookups return the address of
  the pointer to the node, so deletion needs no special case for the head of a chain.
- **TTL**: deadlines live in a min-heap. Each heap item points to its entry's `heap_idx`,
  which the heap updates on every move, so a key finds and removes its timer in O(1).
  - *Lazy*: every access checks the deadline; an expired key is never returned.
  - *Active*: the event loop deletes due keys from the top of the heap, at most 2000 per
    iteration; if more are due, the next `poll()` timeout is 0 and it continues.
- **LRU**: a circular doubly linked list with a dummy head; front = least recently used.
  GET and SET move the entry to the back; EXPIRE does not change recency. When the
  accounted bytes (key + value + 64 bytes overhead) exceed the budget, entries are evicted
  from the front until the new entry fits.

## Sharding

- Only **shards** are on the ring, each at `vnodes` points (`hash(shard_id + "#" + i)`).
  A key belongs to the first point clockwise from `hash(key)`.
- Adding a shard moves about 1/(N+1) of keys instead of about N/(N+1) with `hash % N`.
  More virtual nodes give a more even spread (see [experiments](experiments.md)).
- Clients cache the map and send each command to `primary(owner(key))` over a persistent
  connection. A node that does not own a key answers `MOVED <shard> <primary>`; the client
  reloads the map and retries (up to 3 attempts, with a short back-off).

## Replication

Each shard has one primary and one **dedicated** replica that mirrors exactly that primary.

- **Stream**: `repl.<op> <epoch> <seq> [args]` with ops `reset`, `set <key> <val> <deadline>`,
  `del <key>`, `expireat <key> <deadline>` and a keep-alive `ping`. The replica applies them
  in order and answers each with the last applied sequence number (the acknowledgement).
- **Asynchronous**: the client gets its reply after the primary applies the write; the change
  is queued on the replication connection and sent by the event loop.
- **Absolute deadlines** are replicated, not relative TTLs, so replication delay does not
  extend a key's life.
- **Primary-controlled deletions**: only the primary expires or evicts; those deletions are
  replicated as `del`. The replica keeps the heap and LRU list (ready for promotion) but never
  deletes on its own; expired keys are hidden from its reads until the primary's `del` arrives.
- **Full sync**: when the link (re)connects, the primary queues `reset` followed by every key
  in a single loop iteration, then continues streaming. Later writes land after the snapshot
  in the same ordered TCP stream, so no write pause is needed. While the link is down the
  primary keeps serving and queues nothing; the next full sync covers the gap.
- **Ordering checks**: the replica requires `seq == applied + 1` and an epoch no older than
  its own; otherwise it answers with an error, and the primary drops the link and resyncs.
- **Progress**: `INFO` shows `repl_seq`, `repl_acked` and `repl_lag_ops` on the primary and
  `applied_seq` on the replica.

## Next stages

- **Failure detection**: nodes send heartbeats to the coordinator; a node silent for three
  intervals is declared failed.
- **Promotion**: the coordinator promotes the failed primary's replica (if it is in sync),
  increments the epoch and publishes the map. The ring does not change, so no keys move.
  The old primary rejoins as the replica and receives a full sync.
- **Join**: a new shard registers; affected ranges pause writes, keys are copied to the new
  primary, deleted from the old one (and its replica), and only then is the new map published.
