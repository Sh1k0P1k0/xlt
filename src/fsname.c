/* XLT - file names as the PSP keeps them. See fsname.h. */
#include <stdio.h>
#include <string.h>
#include "fsname.h"
#include "sjis_data.h" /* tools/bake_sjis.py */

/* UTF-8, kept here (not textenc.c) so the gfx.c previews need only this file */
static int fs_utf8_put(uint32_t cp, char *o)
{
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | cp >> 6); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        o[0] = (char)(0xE0 | cp >> 12); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    o[0] = (char)(0xF0 | cp >> 18); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* one UTF-8 character at *s (malformed: U+FFFD, one byte) */
static uint32_t fs_utf8_get(const uint8_t **s, const uint8_t *end)
{
    const uint8_t *p = *s;
    unsigned b = *p++;
    int more = b >= 0xC2 && b <= 0xDF ? 1 : b >= 0xE0 && b <= 0xEF ? 2 : b >= 0xF0 && b <= 0xF4 ? 3 : -1;
    uint32_t cp = more == 1 ? b & 0x1F : more == 2 ? b & 0x0F : b & 0x07;
    for (int i = 0; i < more; i++) {
        if (p >= end || (*p & 0xC0) != 0x80) {
            *s = *s + 1;
            return 0xFFFD;
        }
        cp = cp << 6 | (*p++ & 0x3F);
    }
    if (more < 0) {
        *s = *s + 1;
        return 0xFFFD;
    }
    *s = p;
    return cp;
}

int sjis_char(const unsigned char *s, int avail, uint32_t *cp)
{
    if (avail < 1 || !s[0])
        return 0;
    unsigned b = s[0];
    if (b < 0x80) {
        *cp = b;
        return 1;
    }
    if (b >= 0xA1 && b <= 0xDF) { /* half-width katakana */
        *cp = 0xFF61 + (b - 0xA1);
        return 1;
    }
    int lead = b >= 0x81 && b <= 0x9F ? (int)b - 0x81 : b >= 0xE0 && b <= 0xFC ? (int)b - 0xE0 + 31 : -1;
    if (lead < 0 || avail < 2 || s[1] < 0x40 || s[1] > 0xFC)
        return 0;
    unsigned v = sjis_fwd[lead][s[1] - 0x40];
    if (!v)
        return 0;
    *cp = v;
    return 2;
}

/* 1 if s is UTF-8 through and through: unlike utf8_valid, a character cut off at the
 * end doesn't count (Shift-JIS "E3 82" would pass as the start of one) */
static int utf8_strict(const unsigned char *s)
{
    while (*s) {
        unsigned b = *s;
        int more = b < 0x80 ? 0 : b >= 0xC2 && b <= 0xDF ? 1 : b >= 0xE0 && b <= 0xEF ? 2 : b >= 0xF0 && b <= 0xF4 ? 3 : -1;
        if (more < 0)
            return 0;
        for (int i = 1; i <= more; i++)
            if ((s[i] & 0xC0) != 0x80)
                return 0;
        s += more + 1;
    }
    return 1;
}

void fsname_to_utf8(const char *fs, char *out, int n)
{
    int len = (int)strlen(fs);
    if (n <= 0)
        return;
    if (utf8_strict((const unsigned char *)fs)) {
        snprintf(out, n, "%s", fs);
        return;
    }
    const unsigned char *s = (const unsigned char *)fs, *end = s + len;
    int w = 0;
    while (s < end) {
        uint32_t cp;
        int l = sjis_char(s, (int)(end - s), &cp);
        if (!l) {
            cp = '?';
            l = 1;
        }
        char u[4];
        int k = fs_utf8_put(cp, u);
        if (w + k >= n)
            break;
        memcpy(out + w, u, k);
        w += k;
        s += l;
    }
    out[w] = 0;
}

static int rev_find(uint32_t cp)
{
    int lo = 0, hi = SJIS_REV_N - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (sjis_rev[mid][0] == cp)
            return sjis_rev[mid][1];
        if (sjis_rev[mid][0] < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static int sub_find(uint32_t cp)
{
    int lo = 0, hi = SJIS_SUB_N - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (sjis_sub[mid].cp == cp)
            return mid;
        if (sjis_sub[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

int fsname_from_utf8(const char *utf8, char *out, int n, int *changed)
{
    const uint8_t *s = (const uint8_t *)utf8, *end = s + strlen(utf8);
    int w = 0, ch = 0;
    if (n <= 0)
        return -1;
    while (s < end) {
        unsigned char b[5];
        int k;
        uint32_t cp = *s < 0x80 ? *s++ : fs_utf8_get(&s, end);
        int r = cp < 0x20 || cp == 0x7F ? -1 : cp < 0x80 ? (int)cp : cp < 0x10000 ? rev_find(cp) : -1;
        if (r >= 0 && r < 0x100) {
            b[0] = (unsigned char)r;
            k = 1;
        } else if (r >= 0) {
            b[0] = (unsigned char)(r >> 8);
            b[1] = (unsigned char)r;
            k = 2;
        } else {
            int i = cp < 0x10000 ? sub_find(cp) : -1;
            ch = 1;
            if (i >= 0) {
                k = sjis_sub[i].len;
                memcpy(b, sjis_sub[i].b, k);
            } else {
                b[0] = '_';
                k = 1;
            }
        }
        if (w + k >= n) {
            out[w] = 0;
            if (changed) *changed = ch;
            return -1;
        }
        memcpy(out + w, b, k);
        w += k;
    }
    out[w] = 0;
    if (changed) *changed = ch;
    return 0;
}
