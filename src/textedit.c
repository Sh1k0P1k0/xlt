/* XLT - the Reader's text editor's pure parts. See textedit.h. */
#include <stdlib.h>
#include <string.h>
#include "textedit.h"

/* Length of the valid UTF-8 character at s (not past end), 0 if it isn't one:
 * no overlong forms, surrogates or code points past U+10FFFF. */
static int utf8_char(const uint8_t *s, const uint8_t *end)
{
    uint8_t c = s[0];
    if (c < 0x80) return 1;
    int n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 ? 2 : 0;
    if (!n || c > 0xF4 || end - s < n) return 0;
    for (int i = 1; i < n; i++)
        if ((s[i] & 0xC0) != 0x80) return 0;
    if (c == 0xE0 && s[1] < 0xA0) return 0;  /* overlong */
    if (c == 0xED && s[1] >= 0xA0) return 0; /* surrogate */
    if (c == 0xF0 && s[1] < 0x90) return 0;  /* overlong */
    if (c == 0xF4 && s[1] >= 0x90) return 0; /* past U+10FFFF */
    return n;
}

int ted_check(const uint8_t *s, int n, TedFormat *f, const char **why)
{
    static const char *const W_SIZE = "Only files up to 256 KB can be edited";
    static const char *const W_U16 = "This file is UTF-16: only UTF-8 text can be edited";
    static const char *const W_ENC = "This file isn't UTF-8: only UTF-8 text can be edited";
    static const char *const W_CTRL = "This file has control characters, so it can't be edited";
    static const char *const W_CR = "This file has old Mac line breaks, so it can't be edited";
    f->bom = f->crlf = 0;
    if (n > TED_MAX) { *why = W_SIZE; return 0; }
    if (n >= 2 && ((s[0] == 0xFF && s[1] == 0xFE) || (s[0] == 0xFE && s[1] == 0xFF))) { *why = W_U16; return 0; }
    const uint8_t *p = s, *end = s + n;
    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        f->bom = 1;
        p += 3;
    }
    int lf = 0, crlf = 0;
    while (p < end) {
        uint8_t c = *p;
        if (c == '\r') {
            if (p + 1 >= end || p[1] != '\n') { *why = W_CR; return 0; }
            crlf++;
            p += 2;
            continue;
        }
        if (c == '\n') { lf++; p++; continue; }
        if ((c < 0x20 && c != '\t') || c == 0x7F) { *why = W_CTRL; return 0; }
        int k = utf8_char(p, end);
        if (!k) { *why = W_ENC; return 0; }
        p += k;
    }
    f->crlf = crlf > lf;
    *why = NULL;
    return 1;
}

uint8_t *ted_encode(const char *t, int len, const TedFormat *f, int *out_len)
{
    int lines = 0;
    for (int i = 0; i < len; i++)
        lines += t[i] == '\n';
    uint8_t *out = malloc((size_t)len + (f->crlf ? lines : 0) + 4);
    if (!out) return NULL;
    int k = 0;
    if (f->bom) {
        out[k++] = 0xEF;
        out[k++] = 0xBB;
        out[k++] = 0xBF;
    }
    for (int i = 0; i < len; i++) {
        if (t[i] == '\n' && f->crlf) out[k++] = '\r';
        out[k++] = (uint8_t)t[i];
    }
    *out_len = k;
    return out;
}

int ted_line_start(const char *t, int len, int off)
{
    if (off > len) off = len;
    while (off > 0 && t[off - 1] != '\n')
        off--;
    return off;
}

int ted_line_end(const char *t, int len, int off)
{
    const char *nl = off < len ? memchr(t + off, '\n', (size_t)(len - off)) : NULL;
    return nl ? (int)(nl - t) : len;
}

int ted_line_no(const char *t, int len, int off)
{
    if (off > len) off = len;
    int n = 1;
    for (int i = 0; i < off; i++)
        n += t[i] == '\n';
    return n;
}

int ted_line_count(const char *t, int len) { return ted_line_no(t, len, len); }

char *ted_splice(const char *t, int len, int off, int del, const char *ins, int ins_len, int *out_len)
{
    if (off < 0 || del < 0 || off + del > len) return NULL;
    int n = len - del + ins_len;
    char *out = malloc((size_t)n + 1);
    if (!out) return NULL;
    memcpy(out, t, (size_t)off);
    if (ins_len) memcpy(out + off, ins, (size_t)ins_len);
    memcpy(out + off + ins_len, t + off + del, (size_t)(len - off - del));
    out[n] = 0;
    *out_len = n;
    return out;
}
