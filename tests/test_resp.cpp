#include <gtest/gtest.h>

#include "distcache/protocol/resp.h"

using namespace distcache;

TEST(Resp, EncodesReplies) {
    EXPECT_EQ(resp::simple_string("OK"), "+OK\r\n");
    EXPECT_EQ(resp::integer(3), ":3\r\n");
    EXPECT_EQ(resp::bulk_string("hi"), "$2\r\nhi\r\n");
    EXPECT_EQ(resp::null_bulk(), "$-1\r\n");
}

TEST(Resp, DISABLED_ParsesSetCommand) {
    std::size_t used = 0;
    auto cmd = resp::parse_command("*3\r\n$3\r\nSET\r\n$1\r\na\r\n$1\r\n1\r\n", used);
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->size(), 3u);
    EXPECT_EQ((*cmd)[0], "SET");
}

TEST(Resp, DISABLED_IncompleteReturnsNullopt) {
    std::size_t used = 0;
    EXPECT_FALSE(resp::parse_command("*3\r\n$3\r\nSE", used).has_value());
}
