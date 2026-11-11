#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace distcache {

// 64-bit FNV-1a followed by a final bit mixer.
// FNV-1a alone mixes the low bits poorly for similar inputs ("shard-1#7",
// "shard-1#8"), and both the hashtable (hcode & mask) and the hash ring depend
// on well-spread bits, so the result is passed through a 64-bit finalizer.
inline uint64_t hash64(const void *data, size_t len) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

inline uint64_t hash64(const std::string &s) { return hash64(s.data(), s.size()); }

}  // namespace distcache
