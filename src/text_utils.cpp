#include "text_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline bool is_utf8_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

void utf8_copy_trunc(char *dst, const char *src, size_t n) {
    if (n == 0) return;
    size_t len = strlen(src);
    if (len < n) {
        memcpy(dst, src, len + 1);
        return;
    }
    // Need to cut: keep room for "…" (3 bytes) and the terminator.
    size_t cut = n > 4 ? n - 4 : 0;
    while (cut > 0 && is_utf8_cont((unsigned char)src[cut])) cut--;
    while (cut > 0 && src[cut - 1] == ' ') cut--;
    memcpy(dst, src, cut);
    if (n >= 4) {
        memcpy(dst + cut, "\xE2\x80\xA6", 3);
        cut += 3;
    }
    dst[cut] = '\0';
}

// Latin-1 supplement U+00C0..U+00FF folded to ASCII.
static const char kLatin1Fold[] =
    "AAAAAAACEEEEIIII"   // C0-CF
    "DNOOOOOxOUUUUYTs"   // D0-DF
    "aaaaaaaceeeeiiii"   // E0-EF
    "dnooooo/ouuuuyty";  // F0-FF

void fold_ascii_lower(char *dst, const char *src, size_t n) {
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)src;
    while (*p && o + 1 < n) {
        if (*p < 0x80) {
            dst[o++] = (char)tolower(*p++);
        } else if (p[0] == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {
            dst[o++] = (char)tolower(kLatin1Fold[p[1] - 0x80]);
            p += 2;
        } else if (p[0] == 0xC5 && (p[1] == 0x92 || p[1] == 0x93)) {  // Œ œ
            dst[o++] = 'o';
            if (o + 1 < n) dst[o++] = 'e';
            p += 2;
        } else {
            p++;
            while (*p && is_utf8_cont(*p)) p++;
        }
    }
    dst[o] = '\0';
}

// Encode a code point as UTF-8, returns number of bytes written.
static size_t utf8_encode(char *out, unsigned cp) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    return 0;  // outside the BMP: drop (emoji etc. are not in our fonts)
}

struct NamedEntity { const char *name; unsigned cp; };
static const NamedEntity kEntities[] = {
    {"amp", '&'},     {"lt", '<'},       {"gt", '>'},       {"quot", '"'},
    {"apos", '\''},   {"nbsp", ' '},     {"eacute", 0xE9},  {"egrave", 0xE8},
    {"ecirc", 0xEA},  {"agrave", 0xE0},  {"acirc", 0xE2},   {"ccedil", 0xE7},
    {"icirc", 0xEE},  {"iuml", 0xEF},    {"ocirc", 0xF4},   {"ucirc", 0xFB},
    {"ugrave", 0xF9}, {"Eacute", 0xC9},  {"Agrave", 0xC0},  {"laquo", 0xAB},
    {"raquo", 0xBB},  {"rsquo", '\''},   {"lsquo", '\''},   {"hellip", 0x2026},
    {"euro", 0x20AC}, {"ndash", 0x2013}, {"mdash", 0x2014}, {"deg", 0xB0},
};

// Decode "&...;" starting at s (s[0] == '&'). Returns chars consumed, 0 if not an entity.
static size_t decode_entity(const char *s, char *out, size_t *outLen) {
    const char *semi = (const char *)memchr(s, ';', 12);
    if (!semi) return 0;
    size_t nameLen = semi - s - 1;
    if (nameLen == 0) return 0;
    unsigned cp = 0;
    if (s[1] == '#') {
        cp = (s[2] == 'x' || s[2] == 'X') ? strtoul(s + 3, nullptr, 16) : strtoul(s + 2, nullptr, 10);
        if (cp == 0xA0 || cp == 0x202F) cp = ' ';
        if (cp == 0x2019 || cp == 0x2018) cp = '\'';
    } else {
        for (const auto &e : kEntities) {
            if (strlen(e.name) == nameLen && strncmp(s + 1, e.name, nameLen) == 0) {
                cp = e.cp;
                break;
            }
        }
    }
    if (cp == 0) return 0;
    *outLen = utf8_encode(out, cp);
    return nameLen + 2;
}

static bool tag_is(const char *tag, size_t len, const char *name) {
    size_t n = strlen(name);
    return len == n && strncasecmp(tag, name, n) == 0;
}

void html_to_text(char *dst, const char *src, size_t n) {
    size_t o = 0;
    const char *p = src;
    while (*p && o + 4 < n) {
        if (*p == '<') {
            const char *end = strchr(p, '>');
            if (!end) break;
            const char *tag = p + 1;
            if (*tag == '/') tag++;
            size_t len = 0;
            while (tag + len < end && isalnum((unsigned char)tag[len])) len++;
            if (tag_is(tag, len, "br") || tag_is(tag, len, "p") || tag_is(tag, len, "div") ||
                tag_is(tag, len, "li") || tag_is(tag, len, "ul") || tag_is(tag, len, "tr")) {
                dst[o++] = '\n';
            } else {
                dst[o++] = ' ';
            }
            p = end + 1;
        } else if (*p == '&') {
            char buf[4];
            size_t bl = 0;
            size_t used = decode_entity(p, buf, &bl);
            if (used) {
                for (size_t i = 0; i < bl && o + 1 < n; i++) dst[o++] = buf[i];
                p += used;
            } else {
                dst[o++] = *p++;
            }
        } else {
            dst[o++] = *p++;
        }
    }
    dst[o] = '\0';
    tidy_text(dst);
}

void tidy_text(char *s) {
    // Pass 1: map typographic characters to plain ones (output never grows).
    unsigned char *r = (unsigned char *)s;
    unsigned char *w = r;
    while (*r) {
        if (r[0] == 0xC2 && r[1] == 0xA0) {                 // NBSP
            *w++ = ' '; r += 2;
        } else if (r[0] == 0xE2 && r[1] == 0x80 && (r[2] == 0x98 || r[2] == 0x99)) {
            *w++ = '\''; r += 3;                             // ‘ ’
        } else if (r[0] == 0xE2 && r[1] == 0x80 && (r[2] == 0x9C || r[2] == 0x9D)) {
            *w++ = '"'; r += 3;                              // “ ”
        } else if (r[0] == 0xE2 && r[1] == 0x80 && r[2] == 0xAF) {
            *w++ = ' '; r += 3;                              // narrow NBSP
        } else if (*r == '\t' || *r == '\r') {
            *w++ = ' '; r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';

    // Pass 2: collapse runs of spaces / newlines, trim lines.
    r = w = (unsigned char *)s;
    bool pendingSpace = false, pendingNewline = false;
    while (*r) {
        if (*r == ' ') {
            pendingSpace = true;
        } else if (*r == '\n') {
            pendingNewline = true;
        } else {
            if (w != (unsigned char *)s) {
                if (pendingNewline) *w++ = '\n';
                else if (pendingSpace) *w++ = ' ';
            }
            pendingSpace = pendingNewline = false;
            *w++ = *r;
        }
        r++;
    }
    *w = '\0';
}

time_t utc_mktime(const struct tm *t) {
    // Howard Hinnant's days_from_civil.
    int y = t->tm_year + 1900;
    unsigned m = t->tm_mon + 1;
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t->tm_mday - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + (long)doe - 719468;
    return (time_t)days * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
}

time_t parse_iso8601(const char *s) {
    if (!s) return 0;
    struct tm t = {};
    int consumed = 0;
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%n", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour,
               &t.tm_min, &t.tm_sec, &consumed) != 6)
        return 0;
    t.tm_year -= 1900;
    t.tm_mon -= 1;
    time_t epoch = utc_mktime(&t);
    const char *z = s + consumed;
    if (*z == '.') {
        z++;
        while (isdigit((unsigned char)*z)) z++;
    }
    if (*z == '+' || *z == '-') {
        int hh = 0, mm = 0;
        sscanf(z + 1, "%2d:%2d", &hh, &mm);
        long off = hh * 3600L + mm * 60L;
        epoch += (*z == '+') ? -off : off;
    }
    return epoch;
}
