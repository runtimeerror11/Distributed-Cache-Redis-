#include "distcache/protocol/protocol.h"

#include <cstring>

namespace distcache {

FrameStatus peek_frame(const uint8_t *data, size_t avail, uint32_t *body_len) {
    if (avail < 4) return FrameStatus::Incomplete;
    uint32_t len = 0;
    memcpy(&len, data, 4);
    if (len > k_max_msg) return FrameStatus::TooBig;
    if (4 + size_t(len) > avail) return FrameStatus::Incomplete;
    *body_len = len;
    return FrameStatus::Ready;
}

// ---- Requests ---------------------------------------------------------------

void encode_request(Buffer &out, const std::vector<std::string> &cmd) {
    uint32_t len = 4;
    for (const std::string &s : cmd) len += 4 + static_cast<uint32_t>(s.size());
    out.append_u32(len);
    out.append_u32(static_cast<uint32_t>(cmd.size()));
    for (const std::string &s : cmd) {
        out.append_u32(static_cast<uint32_t>(s.size()));
        out.append(s.data(), s.size());
    }
}

// Cursor helpers: each read checks bounds and advances the cursor.
static bool read_u32(const uint8_t *&cur, const uint8_t *end, uint32_t &out) {
    if (end - cur < 4) return false;
    memcpy(&out, cur, 4);
    cur += 4;
    return true;
}

static bool read_bytes(const uint8_t *&cur, const uint8_t *end, size_t n, std::string &out) {
    if (size_t(end - cur) < n) return false;
    out.assign(reinterpret_cast<const char *>(cur), n);
    cur += n;
    return true;
}

bool parse_request(const uint8_t *data, size_t size, std::vector<std::string> &out) {
    const uint8_t *cur = data, *end = data + size;
    uint32_t nstr = 0;
    if (!read_u32(cur, end, nstr) || nstr > k_max_args) return false;
    out.clear();
    while (out.size() < nstr) {
        uint32_t len = 0;
        if (!read_u32(cur, end, len)) return false;
        out.emplace_back();
        if (!read_bytes(cur, end, len, out.back())) return false;
    }
    return cur == end;  // reject trailing garbage
}

// ---- Responses (server side) ------------------------------------------------

void out_nil(Buffer &out) { out.append_u8(TAG_NIL); }

void out_str(Buffer &out, const std::string &s) {
    out.append_u8(TAG_STR);
    out.append_u32(static_cast<uint32_t>(s.size()));
    out.append(s.data(), s.size());
}

void out_int(Buffer &out, int64_t v) {
    out.append_u8(TAG_INT);
    out.append_i64(v);
}

void out_dbl(Buffer &out, double v) {
    out.append_u8(TAG_DBL);
    out.append_dbl(v);
}

void out_err(Buffer &out, uint32_t code, const std::string &msg) {
    out.append_u8(TAG_ERR);
    out.append_u32(code);
    out.append_u32(static_cast<uint32_t>(msg.size()));
    out.append(msg.data(), msg.size());
}

void out_arr(Buffer &out, uint32_t n) {
    out.append_u8(TAG_ARR);
    out.append_u32(n);
}

size_t response_begin(Buffer &out) {
    size_t header_pos = out.size();
    out.append_u32(0);  // placeholder for the length
    return header_pos;
}

void response_end(Buffer &out, size_t header_pos) {
    size_t msg_size = out.size() - header_pos - 4;
    if (msg_size > k_max_msg) {
        out.truncate(header_pos + 4);
        out_err(out, ERR_TOO_BIG, "response is too big");
        msg_size = out.size() - header_pos - 4;
    }
    uint32_t len = static_cast<uint32_t>(msg_size);
    memcpy(out.data() + header_pos, &len, 4);
}

// ---- Responses (client side) ------------------------------------------------

Value Value::error(uint32_t code, const std::string &msg) {
    Value v;
    v.tag = TAG_ERR;
    v.code = code;
    v.str = msg;
    return v;
}

std::string Value::to_string(int indent) const {
    switch (tag) {
        case TAG_NIL:
            return "(nil)";
        case TAG_ERR:
            return "(err) " + str;
        case TAG_STR:
            return "\"" + str + "\"";
        case TAG_INT:
            return "(int) " + std::to_string(i);
        case TAG_DBL:
            return "(dbl) " + std::to_string(d);
        case TAG_ARR: {
            if (arr.empty()) return "(empty array)";
            std::string s;
            for (size_t k = 0; k < arr.size(); k++) {
                if (k > 0) s += "\n" + std::string(size_t(indent), ' ');
                std::string prefix = std::to_string(k + 1) + ") ";
                s += prefix + arr[k].to_string(indent + int(prefix.size()));
            }
            return s;
        }
    }
    return "(unknown)";
}

static bool parse_value(const uint8_t *&cur, const uint8_t *end, Value *out, int depth) {
    if (depth > 64 || cur >= end) return false;
    out->tag = static_cast<Tag>(*cur++);
    switch (out->tag) {
        case TAG_NIL:
            return true;
        case TAG_ERR: {
            uint32_t len = 0;
            return read_u32(cur, end, out->code) && read_u32(cur, end, len) &&
                   read_bytes(cur, end, len, out->str);
        }
        case TAG_STR: {
            uint32_t len = 0;
            return read_u32(cur, end, len) && read_bytes(cur, end, len, out->str);
        }
        case TAG_INT:
            if (end - cur < 8) return false;
            memcpy(&out->i, cur, 8);
            cur += 8;
            return true;
        case TAG_DBL:
            if (end - cur < 8) return false;
            memcpy(&out->d, cur, 8);
            cur += 8;
            return true;
        case TAG_ARR: {
            uint32_t n = 0;
            if (!read_u32(cur, end, n) || n > k_max_args) return false;
            out->arr.resize(n);
            for (uint32_t k = 0; k < n; k++) {
                if (!parse_value(cur, end, &out->arr[k], depth + 1)) return false;
            }
            return true;
        }
    }
    return false;  // unknown tag
}

bool parse_response(const uint8_t *data, size_t size, Value *out) {
    const uint8_t *cur = data, *end = data + size;
    return parse_value(cur, end, out, 0) && cur == end;
}

}  // namespace distcache
