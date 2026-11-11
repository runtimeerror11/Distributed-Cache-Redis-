#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "distcache/common/buffer.h"

namespace distcache {

// Wire format. TCP is a byte stream with no message boundaries, so every
// message is length-prefixed:
//
//   message  = [u32 body_len][body]
//   request  body = [u32 nstr][u32 len][bytes] ... (a list of strings)
//   response body = one tag-length-value (TLV) value:
//     nil  = [u8 TAG_NIL]
//     err  = [u8 TAG_ERR][u32 code][u32 len][msg]
//     str  = [u8 TAG_STR][u32 len][bytes]
//     int  = [u8 TAG_INT][i64]
//     dbl  = [u8 TAG_DBL][f64]
//     arr  = [u8 TAG_ARR][u32 n] followed by n values
//
// Integers are little-endian (host order on x86 and ARM, so memcpy is enough).

const size_t k_max_msg = 32u << 20;  // 32 MiB, protects against huge length prefixes
const size_t k_max_args = 200 * 1000;

enum Tag : uint8_t {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5,
};

enum ErrCode : uint32_t {
    ERR_UNKNOWN = 1,      // unknown command
    ERR_TOO_BIG = 2,      // response exceeds k_max_msg
    ERR_BAD_ARG = 3,      // wrong arguments
    ERR_MOVED = 4,        // this node does not own the key; message names the owner
    ERR_READONLY = 5,     // write sent to a replica
    ERR_STALE_EPOCH = 6,  // message from an older ownership epoch
    ERR_UNAVAILABLE = 7,  // client side: could not reach the node
};

// ---- Framing --------------------------------------------------------------

enum class FrameStatus { Incomplete, Ready, TooBig };

// Checks whether `avail` bytes start with a complete message.
// On Ready, *body_len is the body length (the message is 4 + body_len bytes).
FrameStatus peek_frame(const uint8_t *data, size_t avail, uint32_t *body_len);

// ---- Requests ---------------------------------------------------------------

void encode_request(Buffer &out, const std::vector<std::string> &cmd);
// Parses a request body. Returns false on malformed input.
bool parse_request(const uint8_t *data, size_t size, std::vector<std::string> &out);

// ---- Responses (server side) ------------------------------------------------

void out_nil(Buffer &out);
void out_str(Buffer &out, const std::string &s);
void out_int(Buffer &out, int64_t v);
void out_dbl(Buffer &out, double v);
void out_err(Buffer &out, uint32_t code, const std::string &msg);
void out_arr(Buffer &out, uint32_t n);

// A response is written straight into the connection's output buffer. Its size
// is unknown until it is written, so 4 bytes are reserved first and patched
// afterwards with the real length.
size_t response_begin(Buffer &out);
void response_end(Buffer &out, size_t header_pos);

// ---- Responses (client side) ------------------------------------------------

struct Value {
    Tag tag = TAG_NIL;
    int64_t i = 0;
    double d = 0;
    uint32_t code = 0;  // for TAG_ERR
    std::string str;    // TAG_STR value or TAG_ERR message
    std::vector<Value> arr;

    static Value error(uint32_t code, const std::string &msg);
    bool is_err() const { return tag == TAG_ERR; }
    // Human-readable form, like a command-line client prints it.
    std::string to_string(int indent = 0) const;
};

// Parses a response body into a Value. Returns false on malformed input.
bool parse_response(const uint8_t *data, size_t size, Value *out);

}  // namespace distcache
