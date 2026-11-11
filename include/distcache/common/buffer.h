#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace distcache {

// A byte FIFO used for connection input/output.
// Appending goes to the back; consuming from the front only moves an offset,
// so removing a processed request is O(1) instead of shifting the whole vector.
// The unused front space is reclaimed once it makes up most of the storage.
class Buffer {
public:
    const uint8_t *data() const { return buf_.data() + start_; }
    uint8_t *data() { return buf_.data() + start_; }
    size_t size() const { return buf_.size() - start_; }
    bool empty() const { return size() == 0; }

    void append(const void *p, size_t n) {
        const uint8_t *b = static_cast<const uint8_t *>(p);
        buf_.insert(buf_.end(), b, b + n);
    }
    void append_u8(uint8_t v) { buf_.push_back(v); }
    // Integers are written in host byte order with memcpy. All supported
    // platforms (x86, ARM) are little-endian, which is the wire format.
    void append_u32(uint32_t v) { append(&v, 4); }
    void append_i64(int64_t v) { append(&v, 8); }
    void append_dbl(double v) { append(&v, 8); }

    // Drop n bytes from the front.
    void consume(size_t n) {
        start_ += n;
        if (start_ >= buf_.size()) {
            buf_.clear();
            start_ = 0;
        } else if (start_ >= 4096 && start_ * 2 >= buf_.size()) {
            buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(start_));
            start_ = 0;
        }
    }
    // Keep only the first n readable bytes.
    void truncate(size_t n) {
        if (n < size()) buf_.resize(start_ + n);
    }
    void clear() {
        buf_.clear();
        start_ = 0;
    }

private:
    std::vector<uint8_t> buf_;
    size_t start_ = 0;
};

}  // namespace distcache
