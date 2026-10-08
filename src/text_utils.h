// Small text helpers (UTF-8 aware) for the data coming from the PRIM API.
#pragma once
#include <stddef.h>
#include <time.h>

// Copy src into dst (size n), cutting on a UTF-8 boundary. Adds "…" when cut.
void utf8_copy_trunc(char *dst, const char *src, size_t n);

// Lowercase ASCII copy of a UTF-8 string with French accents folded (é -> e).
void fold_ascii_lower(char *dst, const char *src, size_t n);

// Strip HTML tags and decode common entities. Paragraphs/<br> become newlines,
// whitespace is collapsed. In-place safe (dst may equal src).
void html_to_text(char *dst, const char *src, size_t n);

// Replace typographic characters our fonts may lack and collapse whitespace.
void tidy_text(char *s);

// "2026-10-08T06:42:00.000Z" (or with +02:00 offset) -> UTC epoch. 0 on error.
time_t parse_iso8601(const char *s);

// UTC broken-down time -> epoch (timegm is not available on newlib).
time_t utc_mktime(const struct tm *t);
