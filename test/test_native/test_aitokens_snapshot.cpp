#include "aitokens/snapshot.h"

#include <unity.h>
#include <cstring>

void test_aitokens_parse_two_sources(void) {
    aitokens::View view;
    const char *line =
        "AITOKENS\tv1\t1\tcodex\t1\tpct\t100\t100% · 88%\t09:00\tclaude\t1\tpct\t88\t88%\t06:10\tcursor\t1\tpct\t87\t3% · 13%\t23 nov";
    TEST_ASSERT_TRUE(aitokens::apply_line(view, line, 1000));
    TEST_ASSERT_TRUE(view.ready);
    TEST_ASSERT_TRUE(view.bridge_ok);
    TEST_ASSERT_FALSE(view.stale);
    TEST_ASSERT_EQUAL(3, view.source_count);
    const aitokens::Source *codex = aitokens::find(view, "codex");
    const aitokens::Source *claude = aitokens::find(view, "claude");
    const aitokens::Source *cursor = aitokens::find(view, "cursor");
    TEST_ASSERT_NOT_NULL(codex);
    TEST_ASSERT_NOT_NULL(claude);
    TEST_ASSERT_NOT_NULL(cursor);
    TEST_ASSERT_EQUAL(int(aitokens::Kind::Pct), int(codex->kind));
    TEST_ASSERT_EQUAL_INT32(100, codex->value);
    TEST_ASSERT_EQUAL_INT32(100, codex->value);
    TEST_ASSERT_EQUAL_STRING("100% · 88%", codex->display);
    TEST_ASSERT_EQUAL_STRING("09:00", codex->reset);
    TEST_ASSERT_EQUAL(int(aitokens::Kind::Pct), int(claude->kind));
    TEST_ASSERT_EQUAL_STRING("88%", claude->display);
    TEST_ASSERT_EQUAL_STRING("06:10", claude->reset);
    TEST_ASSERT_EQUAL_INT32(87, cursor->value);
    TEST_ASSERT_EQUAL_STRING("3% · 13%", cursor->display);
    TEST_ASSERT_EQUAL_STRING("23 nov", cursor->reset);
}

void test_aitokens_rejects_non_aitokens(void) {
    aitokens::View view;
    TEST_ASSERT_FALSE(aitokens::apply_line(view, "TOKEN\tabc", 1));
    TEST_ASSERT_FALSE(view.ready);
}

void test_aitokens_stale_after_quiet(void) {
    aitokens::View view;
    const char *line = "AITOKENS\tv1\t1\tcodex\t1\ttokens\t1\t1\t\t";
    TEST_ASSERT_TRUE(aitokens::apply_line(view, line, 100));
    TEST_ASSERT_FALSE(aitokens::refresh_age(view, 100 + aitokens::kStaleMs - 1));
    TEST_ASSERT_FALSE(view.stale);
    TEST_ASSERT_TRUE(aitokens::refresh_age(view, 100 + aitokens::kStaleMs));
    TEST_ASSERT_TRUE(view.stale);
}

namespace {

aitokens::Source pct_source(const char *id, int remaining, const char *display, const char *reset) {
    aitokens::Source src;
    std::strncpy(src.id, id, sizeof(src.id) - 1);
    src.available = true;
    src.kind = aitokens::Kind::Pct;
    src.value = remaining;
    std::strncpy(src.display, display, sizeof(src.display) - 1);
    if (reset) std::strncpy(src.reset, reset, sizeof(src.reset) - 1);
    return src;
}

}  // namespace

void test_claude_home_row_uses_remaining_percent(void) {
    char out[80];
    aitokens::RowLevel level = aitokens::RowLevel::Alert;
    const aitokens::Source plenty = pct_source("claude", 92, "92%", "");
    TEST_ASSERT_EQUAL_INT(92, aitokens::home_row_text("Claude", plenty, false, out, sizeof(out), &level));
    TEST_ASSERT_EQUAL_STRING("Claude · 92% restante", out);
    TEST_ASSERT_EQUAL(int(aitokens::RowLevel::Ok), int(level));

    const aitokens::Source almost_gone = pct_source("claude", 8, "8%", "");
    TEST_ASSERT_EQUAL_INT(8, aitokens::home_row_text("Claude", almost_gone, false, out, sizeof(out), &level));
    TEST_ASSERT_EQUAL_STRING("Claude · 8% restante · alerta", out);
    TEST_ASSERT_EQUAL(int(aitokens::RowLevel::Alert), int(level));

    const aitokens::Source remembered = pct_source("claude", 92, "92% antigo", "");
    aitokens::home_row_text("Claude", remembered, false, out, sizeof(out), &level);
    TEST_ASSERT_EQUAL_STRING("Claude · 92% antigo restante", out);
    TEST_ASSERT_EQUAL(int(aitokens::RowLevel::Ok), int(level));
}

void test_codex_and_cursor_home_rows_keep_their_shape(void) {
    char out[80];
    aitokens::RowLevel level = aitokens::RowLevel::Ok;
    const aitokens::Source codex = pct_source("codex", 40, "40% · 10%", "09:00");
    TEST_ASSERT_EQUAL_INT(40, aitokens::home_row_text("Codex", codex, false, out, sizeof(out), &level));
    TEST_ASSERT_EQUAL_STRING("Codex · 40% restante · baixo · 09:00", out);
    TEST_ASSERT_EQUAL(int(aitokens::RowLevel::Low), int(level));

    const aitokens::Source cursor = pct_source("cursor", 87, "3% · 13%", "23 nov");
    TEST_ASSERT_EQUAL_INT(87, aitokens::home_row_text("Cursor", cursor, false, out, sizeof(out), &level));
    TEST_ASSERT_EQUAL_STRING("Cursor · Modelos 3% · Outros 13% · 23 nov", out);
    TEST_ASSERT_EQUAL(int(aitokens::RowLevel::Ok), int(level));
}

void test_aitokens_unavailable_sources(void) {
    aitokens::View view;
    const char *line = "AITOKENS\tv1\t1\tcodex\t0\tnone\t0\t—\t\tclaude\t0\tnone\t0\t—\t";
    TEST_ASSERT_TRUE(aitokens::apply_line(view, line, 50));
    const aitokens::Source *claude = aitokens::find(view, "claude");
    TEST_ASSERT_NOT_NULL(claude);
    TEST_ASSERT_FALSE(claude->available);
}
