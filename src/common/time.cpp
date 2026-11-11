#include "distcache/common/time.h"

#include <time.h>

namespace distcache {

static uint64_t read_clock_us(clockid_t id) {
    timespec tv = {0, 0};
    clock_gettime(id, &tv);
    return uint64_t(tv.tv_sec) * 1000000 + uint64_t(tv.tv_nsec) / 1000;
}

uint64_t mono_us() { return read_clock_us(CLOCK_MONOTONIC); }
uint64_t mono_ms() { return mono_us() / 1000; }
uint64_t wall_ms() { return read_clock_us(CLOCK_REALTIME) / 1000; }

}  // namespace distcache
