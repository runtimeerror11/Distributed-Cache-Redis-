#pragma once
#include <cstdint>

namespace distcache {

// Monotonic clock: only moves forward. Used for durations, timeouts and timers
// that stay inside one process (idle connections, reconnect timers, latency).
uint64_t mono_ms();
uint64_t mono_us();

// Wall clock: milliseconds since the Unix epoch. Used for TTL deadlines because
// they are copied to replicas, and a monotonic value from one machine means
// nothing on another. Assumes the machines' clocks are synchronized (NTP).
uint64_t wall_ms();

}  // namespace distcache
