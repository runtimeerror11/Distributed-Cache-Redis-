# Write acknowledgement and consistency policy

_TODO: finalise once replication is implemented._

| Policy | Client gets OK when | Data loss on primary crash | Latency |
|---|---|---|---|
| `async` (default) | primary applied the write | possible (writes not yet replicated) | lowest |
| `replica` | primary **and** replica applied it | none for acknowledged writes | higher |

- Reads go to the primary → **read-your-writes** for a single client.
- Replicas are **eventually consistent** with the primary.
- Ownership changes are ordered by **epoch**; stale-epoch writes are rejected.
