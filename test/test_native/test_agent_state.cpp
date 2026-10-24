#include "agent/state.h"

#include <unity.h>
#include <cstring>

void test_agent_ok_line(void) {
    agent::View v{};
    TEST_ASSERT_TRUE(agent::apply_line(v, "AGENT\tv1\t1", 1000));
    TEST_ASSERT_TRUE(v.ready);
    TEST_ASSERT_TRUE(v.ok);
    TEST_ASSERT_EQUAL_UINT32(1000, v.received_ms);
}

void test_agent_fail_with_reason(void) {
    agent::View v{};
    TEST_ASSERT_TRUE(agent::apply_line(v, "AGENT\tv1\t0\tserial busy", 2000));
    TEST_ASSERT_TRUE(v.ready);
    TEST_ASSERT_FALSE(v.ok);
    TEST_ASSERT_EQUAL_STRING("serial busy", v.detail);
}

void test_agent_ignores_garbage(void) {
    agent::View v{};
    TEST_ASSERT_FALSE(agent::apply_line(v, "AGENT\tv0\t1", 1));
    TEST_ASSERT_FALSE(agent::apply_line(v, "AITOKENS\tv1\t1", 1));
    TEST_ASSERT_FALSE(agent::apply_line(v, "AGENT?", 1));
    TEST_ASSERT_FALSE(v.ready);
}
