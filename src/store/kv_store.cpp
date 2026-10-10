#include "distcache/store/kv_store.h"

namespace distcache {

KvStore::KvStore(std::size_t max_memory_bytes) : max_memory_bytes_(max_memory_bytes) {}

std::optional<std::string> KvStore::get(const std::string& key) {
    // TODO(stage 1): lock, look up key, lazily delete if expired, update LRU, count hit/miss.
    (void)key;
    return std::nullopt;
}

void KvStore::set(const std::string& key, std::string value) {
    // TODO(stage 1): insert/overwrite (SET clears any TTL), update memory usage + LRU,
    // then evict_if_needed_locked().
    (void)key;
    (void)value;
}

bool KvStore::del(const std::string& key) {
    // TODO(stage 1): erase key, update memory usage + LRU. Return true if it existed.
    (void)key;
    return false;
}

bool KvStore::expire(const std::string& key, std::chrono::seconds ttl) {
    // TODO(stage 1): set expires_at = now + ttl. Return false if key is missing.
    (void)key;
    (void)ttl;
    return false;
}

long long KvStore::ttl(const std::string& key) {
    // TODO(stage 1)
    (void)key;
    return -2;
}

std::size_t KvStore::sweep_expired(std::size_t sample_size) {
    // TODO(stage 1): periodic expiry. Redis-style: sample keys that have a TTL,
    // delete expired ones, repeat while >25% of the sample was expired.
    (void)sample_size;
    return 0;
}

std::size_t KvStore::size() const {
    std::lock_guard lock(mu_);
    return data_.size();
}

std::size_t KvStore::memory_used() const {
    std::lock_guard lock(mu_);
    return memory_used_;
}

bool KvStore::is_expired_locked(const Entry& e) const {
    return e.expires_at.has_value() && Clock::now() >= *e.expires_at;
}

void KvStore::evict_if_needed_locked() {
    // TODO(stage 1): while memory_used_ > max_memory_bytes_, remove lru_.victim().
}

}  // namespace distcache
