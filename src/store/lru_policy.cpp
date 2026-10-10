#include "distcache/store/lru_policy.h"

namespace distcache {

void LruPolicy::touch(const std::string& key) {
    // TODO(stage 1): if present, splice its node to the front; else push_front and index it.
    (void)key;
}

void LruPolicy::remove(const std::string& key) {
    // TODO(stage 1)
    (void)key;
}

std::optional<std::string> LruPolicy::victim() const {
    if (order_.empty()) return std::nullopt;
    return order_.back();
}

}  // namespace distcache
