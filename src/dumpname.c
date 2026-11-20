/* XLT - the portable parts of copying a UMD to an ISO. See dumpname.h. */
#include <stdio.h>
#include <string.h>
#include "dumpname.h"

static int bad_char(unsigned char c) { return c < 0x20 || c == 0x7F || strchr("\\/:*?\"<>|", c); }

/* s cleaned into out (n bytes): bad characters as spaces, one space at most in a row,
 * none at the start; trailing spaces and dots removed. */
static void clean(const char *s, char *out, int n)
{
    int o = 0;
    for (; *s && o < n - 1; s++) {
        unsigned char c = bad_char((unsigned char)*s) ? ' ' : (unsigned char)*s;
        if (c == ' ' && (o == 0 || out[o - 1] == ' '))
            continue;
        out[o++] = (char)c;
    }
    while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '.'))
        o--;
    out[o] = 0;
}

/* Cuts s to at most max bytes, not inside a UTF-8 character, and cleans the end again. */
static void cut(char *s, int max)
{
    int l = (int)strlen(s);
    if (l <= max)
        return;
    l = max;
    while (l > 0 && ((unsigned char)s[l] & 0xC0) == 0x80) /* s[l] continues a character: drop it all */
        l--;
    while (l > 0 && (s[l - 1] == ' ' || s[l - 1] == '.'))
        l--;
    s[l] = 0;
}

void dump_file_name(const char *title, const char *id, char *out, int n)
{
    char t[256], d[32];
    clean(title ? title : "", t, sizeof(t));
    clean(id ? id : "", d, sizeof(d));
    while (t[0] == '.') /* no hidden-looking names */
        memmove(t, t + 1, strlen(t));
    char tail[48];
    if (d[0]) snprintf(tail, sizeof(tail), "%s[%s].iso", t[0] ? " " : "", d);
    else snprintf(tail, sizeof(tail), ".iso");
    int room = DUMP_NAME_MAX - (int)strlen(tail);
    if (room > n - 1 - (int)strlen(tail)) room = n - 1 - (int)strlen(tail);
    cut(t, room < 0 ? 0 : room);
    if (!t[0] && !d[0]) snprintf(out, n, "UMD.iso");
    else if (!t[0]) snprintf(out, n, "[%s].iso", d);
    else snprintf(out, n, "%s%s", t, tail);
}

uint32_t dump_pvd_sectors(const uint8_t *s)
{
    if (s[0] != 1 || memcmp(s + 1, "CD001", 5))
        return 0;
    uint32_t le = s[80] | s[81] << 8 | s[82] << 16 | (uint32_t)s[83] << 24;
    uint32_t be = (uint32_t)s[84] << 24 | s[85] << 16 | s[86] << 8 | s[87];
    return le == be ? le : 0;
}

uint32_t dump_disc_sectors(int end, uint32_t pvd, int end_reads)
{
    if (end <= 16)
        return pvd > 16 ? pvd : 0;
    uint32_t e = (uint32_t)end;
    if (pvd == e || pvd == e + 1)
        return pvd;       /* they agree, the drive's way or the emulator's */
    return end_reads ? e + 1 : e;
}

void dump_time_left(uint64_t done, uint64_t total, uint32_t elapsed_ms, char *out, int n)
{
    out[0] = 0;
    if (elapsed_ms < 5000 || done < (1u << 20) || done >= total)
        return;
    uint64_t left_s = (total - done) * elapsed_ms / done / 1000;
    if (left_s < 60) {
        snprintf(out, n, "less than a minute left");
        return;
    }
    unsigned m = (unsigned)((left_s + 30) / 60);
    if (m < 60) snprintf(out, n, "about %u min left", m);
    else if (m % 60 == 0) snprintf(out, n, "about %u h left", m / 60);
    else snprintf(out, n, "about %u h %u min left", m / 60, m % 60);
}
