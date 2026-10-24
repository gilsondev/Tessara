#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure text helpers: UTF-8 safe copies, URL encoding and pt-BR rendering of Todoist fields.
namespace text {

void copy(char *dest, size_t cap, const char *src);
void url_encode(char *dest, size_t cap, const char *src);
// Skips leading emoji/space decoration ("🏠 Casa" -> "Casa").
const char *visible_name(const char *name);
// "every monday at 9am" -> "toda segunda às 9h". False when the phrase is not understood.
bool localize_due(const char *due_string, char *dest, size_t cap);
// "2026-10-23" -> "23 out", "2026-10-23T14:05" -> "23 out, 14:05".
bool format_date(const char *iso, char *dest, size_t cap);
// Markdown title to plain text, dropping glyphs the UI font lacks (reported through on_missing).
void clean_title(char *dest, size_t cap, const char *src, void (*on_missing)(uint32_t codepoint));

}  // namespace text
