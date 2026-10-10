#pragma once
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "distcache/common/types.h"
#include "distcache/store/lru_policy.h"

namespace distcache {

// One entry in the store. `expires_at` is empty when the key has no TTL.
struct Entry {
    std::string value;
    std::optional<TimePoint> expires_at;
};

// Thread-safe, bounded-memory, in-memory key-value store for a single node.
//
// Expiry:   lazy (checked on every access) + periodic (background sweep of a random sample).
// Eviction: when memory usage exceeds `max_memory_bytes`, evict using the LRU policy.
class KvStore {
public:
    explicit KvStore(std::size_t max_memory_bytes);

    std::optional<std::string> get(const std::string& key);
    void set(const std::string& key, std::string value);
    bool del(const std::string& key);
    bool expire(const std::string& key, std::chrono::seconds ttl);
    long long ttl(const std::string& key);  // -2 = missing, -1 = no TTL, else seconds left

    // Removes up to `sample_size` expired keys. Called by the periodic expiry thread.
    std::size_t sweep_expired(std::size_t sample_size);

    std::size_t size() const;
    std::size_t memory_used() const;

    // Statistics used for hit-rate experiments.
    std::uint64_t hits() const { return hits_; }
    std::uint64_t misses() const { return misses_; }
    std::uint64_t evictions() const { return evictions_; }

private:
    bool is_expired_locked(const Entry& e) const;
    void evict_if_needed_locked();

    mutable std::mutex mu_;
    std::unordered_map<std::string, Entry> data_;
    LruPolicy lru_;
    std::size_t max_memory_bytes_;
    std::size_t memory_used_ = 0;
    std::uint64_t hits_ = 0, misses_ = 0, evictions_ = 0;
};

}  // namespace distcache
