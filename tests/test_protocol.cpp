#include <cstring>

#include "check.h"
#include "distcache/protocol/protocol.h"

using namespace distcache;

TEST(protocol_request_roundtrip) {
    Buffer b;
    std::string binary("a\0b\nc", 5);  // values may contain any bytes
    encode_request(b, {"set", "key", binary});
    uint32_t len = 0;
    CHECK(peek_frame(b.data(), b.size(), &len) == FrameStatus::Ready);
    CHECK_EQ(size_t(4 + len), b.size());
    std::vector<std::string> cmd;
    CHECK(parse_request(b.data() + 4, len, cmd));
    CHECK_EQ(cmd.size(), size_t(3));
    CHECK(cmd[2] == binary);
}

TEST(protocol_partial_and_pipelined_frames) {
    Buffer b;
    encode_request(b, {"get", "a"});
    encode_request(b, {"get", "b"});
    uint32_t len = 0;
    // TCP may deliver any prefix: every prefix shorter than one message is incomplete.
    uint32_t first = 0;
    peek_frame(b.data(), b.size(), &first);
    for (size_t n = 0; n < 4 + first; n++) {
        CHECK(peek_frame(b.data(), n, &len) == FrameStatus::Incomplete);
    }
    // Two messages in one read are processed one after the other.
    std::vector<std::string> cmd;
    CHECK(peek_frame(b.data(), b.size(), &len) == FrameStatus::Ready);
    CHECK(parse_request(b.data() + 4, len, cmd) && cmd[1] == "a");
    b.consume(4 + len);
    CHECK(peek_frame(b.data(), b.size(), &len) == FrameStatus::Ready);
    CHECK(parse_request(b.data() + 4, len, cmd) && cmd[1] == "b");
}

TEST(protocol_rejects_bad_input) {
    uint8_t huge[4];
    uint32_t big = uint32_t(k_max_msg + 1);
    memcpy(huge, &big, 4);
    uint32_t len = 0;
    CHECK(peek_frame(huge, 4, &len) == FrameStatus::TooBig);

    Buffer b;
    encode_request(b, {"get", "a"});
    b.append_u8(0);  // trailing garbage inside the body length
    uint32_t body = uint32_t(b.size() - 4);
    memcpy(b.data(), &body, 4);
    std::vector<std::string> cmd;
    CHECK(!parse_request(b.data() + 4, body, cmd));
}

TEST(protocol_response_values) {
    Buffer out;
    size_t h = response_begin(out);
    out_arr(out, 4);
    out_str(out, "hello");
    out_int(out, -2);
    out_nil(out);
    out_err(out, ERR_MOVED, "MOVED s2 127.0.0.1:7201");
    response_end(out, h);

    uint32_t len = 0;
    CHECK(peek_frame(out.data(), out.size(), &len) == FrameStatus::Ready);
    Value v;
    CHECK(parse_response(out.data() + 4, len, &v));
    CHECK(v.tag == TAG_ARR && v.arr.size() == 4);
    CHECK(v.arr[0].tag == TAG_STR && v.arr[0].str == "hello");
    CHECK(v.arr[1].tag == TAG_INT && v.arr[1].i == -2);
    CHECK(v.arr[2].tag == TAG_NIL);
    CHECK(v.arr[3].is_err() && v.arr[3].code == ERR_MOVED);
}

TEST(protocol_header_patched_after_existing_data) {
    // A second response is appended after unsent bytes; its header is patched in place.
    Buffer out;
    out.append("xyz", 3);
    size_t h = response_begin(out);
    out_str(out, "abc");
    response_end(out, h);
    uint32_t len = 0;
    memcpy(&len, out.data() + 3, 4);
    CHECK_EQ(len, uint32_t(1 + 4 + 3));
}
