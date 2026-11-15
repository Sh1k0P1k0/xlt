/* xltsync - which paths the PC may read and change. See syncpath.h. */
#include <string.h>
#include "syncpath.h"

static int lower(int ch) { return ch >= 'A' && ch <= 'Z' ? ch + 32 : ch; }

/* 1 if the first n characters of a equal b (b lower case), ignoring case */
static int prefix_ci(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++)
        if (lower((unsigned char)a[i]) != b[i])
            return 0;
    return 1;
}

/* The folders the PC may change things in: XLT's media and game folders. */
static const char *const ROOTS[] = {"iso", "video", "music", "picture", "books", "downloads", "psp/game", NULL};
/* Save data: only a save folder and the files directly in it. */
#define SAVES "psp/savedata"
#define SAVES_LEN 12

/* the part after "ms0:/" if it starts with root (case-insensitive) and then "/" or its end; else NULL */
static const char *under(const char *rest, const char *root, int n)
{
    if (!prefix_ci(rest, root, n) || (rest[n] != '/' && rest[n]))
        return NULL;
    return rest + n;
}

int path_save_depth(const char *p)
{
    if (strncmp(p, "ms0:/", 5) && strncmp(p, "ef0:/", 5))
        return 0;
    const char *t = under(p + 5, SAVES, SAVES_LEN);
    if (!t || t[0] != '/' || !t[1])
        return 0;
    return strchr(t + 1, '/') ? 2 : 1;
}

int path_ok(const char *p, int mode)
{
    /* the firmware's fonts, to read only (XLT draws kanji from flash0:/font/jpn0.pgf) */
    if (!strncmp(p, "flash0:/font", 12) && (p[12] == 0 || p[12] == '/'))
        return mode == P_READ && !strstr(p, "..") && !strstr(p, "//");
    if (strncmp(p, "ms0:/", 5) && strncmp(p, "ef0:/", 5))
        return 0;
    const char *rest = p + 5;
    int len = strlen(p);
    if (len >= 240)
        return 0;
    if (!*rest)
        return mode == P_READ;
    const char *seg = rest;
    for (const char *q = rest;; q++) {
        unsigned char ch = *q;
        if (ch == '/' || !ch) {
            int n = q - seg;
            if (n == 0 || (n == 1 && seg[0] == '.') || (n == 2 && seg[0] == '.' && seg[1] == '.'))
                return 0;
            if (seg[n - 1] == ' ' || seg[n - 1] == '.') /* FAT drops these: two names, one file */
                return 0;
            if (!ch)
                break;
            seg = q + 1;
        } else if ((ch >= 0x81 && ch <= 0x9F) || (ch >= 0xE0 && ch <= 0xFC)) {
            /* Shift-JIS, which is what the PSP's FAT driver takes names in: a lead
             * byte and a trail byte, which may be '\' or '|' (ソ is 83 5C) */
            unsigned char t = q[1];
            if (!((t >= 0x40 && t <= 0x7E) || (t >= 0x80 && t <= 0xFC)))
                return 0;
            q++;
        } else if (ch < 0x20 || ch == 0x7F || ch == 0x80 || ch == 0xA0 || ch >= 0xFD ||
                   strchr("\\:*?\"<>|", ch)) {
            return 0; /* (0xA1..0xDF: half-width katakana, one byte each) */
        }
    }
    if (mode == P_READ)
        return 1;
    const char *t = under(rest, SAVES, SAVES_LEN);
    if (t) {
        if (!*t)
            return mode == P_MKDIR;
        /* "/<save>" or "/<save>/<file>": at most one more slash */
        const char *s = strchr(t + 1, '/');
        return !s || !strchr(s + 1, '/');
    }
    for (int i = 0; ROOTS[i]; i++) {
        t = under(rest, ROOTS[i], strlen(ROOTS[i]));
        if (t)
            return *t ? 1 : mode == P_MKDIR;
    }
    return 0;
}

int path_within(const char *inner, const char *outer)
{
    int n = strlen(outer);
    while (n > 0 && outer[n - 1] == '/')
        n--;
    for (int i = 0; i < n; i++)
        if (lower((unsigned char)inner[i]) != lower((unsigned char)outer[i]))
            return 0;
    return inner[n] == 0 || inner[n] == '/';
}
