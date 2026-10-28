#include <unity.h>

#include "platform/boot_notice.h"

// controller::on_agent calls console::send_boot_reason_once after a valid AGENT line.
// That call is this take(): the first one writes BOOT, the next one writes nothing.
void test_boot_reason_is_sent_once_after_agent(void) {
    BootNotice notice;
    notice.set("panic");
    char first[64];
    char second[64] = "untouched";
    TEST_ASSERT_TRUE(notice.take(first, sizeof(first)));
    TEST_ASSERT_EQUAL_STRING("BOOT\tpanic\n", first);
    TEST_ASSERT_FALSE(notice.take(second, sizeof(second)));
    TEST_ASSERT_EQUAL_STRING("untouched", second);
}

void test_panic_line_lists_pc_cause_and_backtrace(void) {
    const uint32_t bt[] = {0x42001234u, 0x4008abcdu};
    char line[96];
    TEST_ASSERT_TRUE(format_panic_line(line, sizeof(line), 0x40371234u, 29, bt, 2));
    TEST_ASSERT_EQUAL_STRING("PANIC\t40371234\t1d\t42001234,4008abcd\n", line);
}
