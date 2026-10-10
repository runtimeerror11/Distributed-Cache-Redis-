#pragma once
#include <list>
#include <optional>
#include <string>
#include <unordered_map>

namespace distcache {

// O(1) LRU bookkeeping: doubly linked list (most recent at front) + hash map of iterators.
// Not thread-safe on its own; KvStore holds the lock.
class LruPolicy {
public:
    void touch(const std::string& key);           // record an access (insert or move to front)
    void remove(const std::string& key);          // forget a key (DEL / expiry)
    std::optional<std::string> victim() const;    // least recently used key, if any
    std::size_t size() const { return index_.size(); }

private:
    std::list<std::string> order_;
    std::unordered_map<std::string, std::list<std::string>::iterator> index_;
};

}  // namespace distcache
