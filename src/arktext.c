/* XLT - SETTINGS.TXT clock lines as pure text functions (host-testable).
 * Lines are "runlevel, key, on|off"; a game ID as runlevel scopes the line
 * to that game. Only clock lines for the requested runlevel are replaced;
 * every other line is kept byte for byte (blank lines are dropped). */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ark.h"
#include "arktext.h"

/* --------------------------------------------------------------- parsing */

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = 0;
    return s;
}

/* Splits "a, b, c" into 3 trimmed parts in `tmp`. Returns 1 if it has exactly 3. */
static int split3(const char *line, char *tmp, int n, char **p)
{
    snprintf(tmp, n, "%s", line);
    char *c1 = strchr(tmp, ','), *c2 = c1 ? strchr(c1 + 1, ',') : NULL;
    if (!c1 || !c2 || strchr(c2 + 1, ','))
        return 0;
    *c1 = *c2 = 0;
    p[0] = trim(tmp);
    p[1] = trim(c1 + 1);
    p[2] = trim(c2 + 1);
    return 1;
}

/* CLOCK_* for a clock key, or -1 if the key is not about the clock. */
static int clock_of_key(const char *k)
{
    if (!strcasecmp(k, "cpuclock:133") || !strcasecmp(k, "powersave")) return CLOCK_133;
    if (!strcasecmp(k, "cpuclock:222") || !strcasecmp(k, "defaultclock")) return CLOCK_222;
    if (!strcasecmp(k, "cpuclock:333") || !strcasecmp(k, "overclock")) return CLOCK_333;
    if (!strncasecmp(k, "cpuclock", 8)) return CLOCK_AUTO; /* other speeds: treated as ours to replace */
    return -1;
}

static int is_on(const char *v)
{
    return !strcasecmp(v, "on") || !strcmp(v, "1") || !strcasecmp(v, "enabled") || !strcasecmp(v, "true");
}

/* Does this runlevel cover `id` (or "all games" when id is NULL)? */
static int level_matches(const char *rl, const char *id, int strict)
{
    if (id)
        return !strcasecmp(rl, id);
    if (!strcasecmp(rl, "game"))
        return 1;
    return !strict && (!strcasecmp(rl, "always") || !strcasecmp(rl, "all"));
}

int arktext_get_clock(const char *text, const char *id)
{
    int clock = CLOCK_AUTO;
    const char *line = text;
    while (*line) {
        const char *nl = strchr(line, '\n');
        int n = nl ? (int)(nl - line) : (int)strlen(line);
        char raw[160], tmp[160], *p[3];
        snprintf(raw, sizeof(raw), "%.*s", n < 159 ? n : 159, line);
        char *cr = strchr(raw, '\r');
        if (cr) *cr = 0;
        if (split3(raw, tmp, sizeof(tmp), p)) {
            int c = clock_of_key(p[1]);
            if (c >= 0 && level_matches(p[0], id, 0) && is_on(p[2]))
                clock = c; /* the last matching line wins */
        }
        line += n + (nl ? 1 : 0);
    }
    return clock;
}

static int put(char *out, int *len, int cap, const char *s, int n)
{
    if (*len + n >= cap)
        return -1;
    memcpy(out + *len, s, n);
    *len += n;
    return 0;
}

int arktext_set_clock(const char *text, const char *id, int clock, int ark4, char *out, int cap)
{
    int len = 0, err = 0;
    char buf[200];
    const char *line = text;
    while (*line) {
        const char *nl = strchr(line, '\n');
        int n = nl ? (int)(nl - line) : (int)strlen(line);
        int keep = n;
        while (keep > 0 && (line[keep - 1] == '\r'))
            keep--;
        char raw[160], tmp[160], *p[3];
        int parsed = 0;
        if (keep < (int)sizeof(raw)) {
            snprintf(raw, sizeof(raw), "%.*s", keep, line);
            parsed = split3(raw, tmp, sizeof(tmp), p) && clock_of_key(p[1]) >= 0;
        }
        if (parsed && level_matches(p[0], id, 1)) {
            /* replaced below */
        } else if (parsed && !id && (!strcasecmp(p[0], "always") || !strcasecmp(p[0], "all"))) {
            /* "always" also covers games: keep it for the XMB only */
            int m = snprintf(buf, sizeof(buf), "vsh, %s, %s\n", p[1], p[2]);
            err |= put(out, &len, cap, buf, m);
        } else if (keep >= (int)sizeof(raw) || trim(raw)[0]) {
            err |= put(out, &len, cap, line, keep);
            err |= put(out, &len, cap, "\n", 1);
        }
        line += n + (nl ? 1 : 0);
    }
    if (clock > CLOCK_AUTO && clock < CLOCK_COUNT) {
        static const char *k5[] = {"", "cpuclock:133", "cpuclock:222", "cpuclock:333"};
        static const char *k4[] = {"", "powersave", "defaultclock", "overclock"};
        int m = snprintf(buf, sizeof(buf), "%s, %s, on\n", id ? id : "game", ark4 ? k4[clock] : k5[clock]);
        err |= put(out, &len, cap, buf, m);
    }
    return err ? -1 : len;
}

/* Runlevels that can lock XLT's clock: ARK's "game" also matches homebrew (apitype
 * 0x141/0x152), "always" everything, and a runlevel with '/' matches the file
 * that was started. */
static int level_covers_app(const char *rl, const char *path)
{
    return !strcasecmp(rl, "game") || !strcasecmp(rl, "always") || !strcasecmp(rl, "all") ||
           !strcasecmp(rl, "homebrew") || !strcasecmp(rl, "app") || !strcasecmp(rl, path);
}

/* ARK-5's settingsHandler: an "on" line sets the clock; an "off" line clears it only when
 * the clock set is that line's own speed ("cpuclock:133, off" doesn't undo 333); an
 * off custom "cpuclock:N" clears the custom speed. Returns the clock class XLT gets
 * (CLOCK_133..CLOCK_333, CLOCK_AUTO for a custom speed), or -1 when free. Up to 8
 * distinct keys of lines that set it go to keys[]. */
static int ark_clock_for(const char *text, const char *path, char keys[][40], int *nkeys)
{
    int state = -1;
    if (nkeys) *nkeys = 0;
    const char *line = text;
    while (*line) {
        const char *nl = strchr(line, '\n');
        int n = nl ? (int)(nl - line) : (int)strlen(line);
        char raw[160], tmp[160], *p[3];
        snprintf(raw, sizeof(raw), "%.*s", n < 159 ? n : 159, line);
        char *cr = strchr(raw, '\r');
        if (cr) *cr = 0;
        int c;
        if (split3(raw, tmp, sizeof(tmp), p) && (c = clock_of_key(p[1])) >= 0 && level_covers_app(p[0], path)) {
            if (is_on(p[2])) {
                state = c;
                if (nkeys) {
                    int dup = 0;
                    for (int k = 0; k < *nkeys; k++)
                        dup |= !strcasecmp(keys[k], p[1]);
                    if (!dup && *nkeys < 8 && strlen(p[1]) < 40)
                        snprintf(keys[(*nkeys)++], 40, "%s", p[1]);
                }
            } else if (state == c) {
                state = -1;
            }
        }
        line += n + (nl ? 1 : 0);
    }
    return state;
}

int arktext_free_clock(const char *text, const char *path, int ark4, char *out, int cap)
{
    if (!path || !path[0] || strchr(path, ',') || strlen(path) > 120)
        return -1;
    (void)ark4; /* the off lines use the keys as the file spells them */
    char keys[8][40];
    int nkeys = 0;
    if (ark_clock_for(text, path, keys, &nkeys) < 0)
        return 0;
    /* Every line as it was, except clock lines for `path`; then an off line for each
     * speed a line that covers XLT sets, last (any order clears whichever is set). */
    int len = 0, err = 0;
    char buf[200];
    const char *line = text;
    while (*line) {
        const char *nl = strchr(line, '\n');
        int n = nl ? (int)(nl - line) : (int)strlen(line);
        int keep = n;
        while (keep > 0 && line[keep - 1] == '\r')
            keep--;
        char raw[160], tmp[160], *p[3];
        int drop = 0;
        if (keep < (int)sizeof(raw)) {
            snprintf(raw, sizeof(raw), "%.*s", keep, line);
            drop = split3(raw, tmp, sizeof(tmp), p) && clock_of_key(p[1]) >= 0 && !strcasecmp(p[0], path);
        }
        if (!drop && (keep >= (int)sizeof(raw) || trim(raw)[0])) {
            err |= put(out, &len, cap, line, keep);
            err |= put(out, &len, cap, "\n", 1);
        }
        line += n + (nl ? 1 : 0);
    }
    for (int k = 0; k < nkeys; k++) {
        int m = snprintf(buf, sizeof(buf), "%s, %s, off\n", path, keys[k]);
        err |= put(out, &len, cap, buf, m);
    }
    if (err)
        return -1;
    out[len < cap ? len : cap - 1] = 0;
    return ark_clock_for(out, path, NULL, NULL) < 0 ? len : -1; /* still locked: say so */
}
