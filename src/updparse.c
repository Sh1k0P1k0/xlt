/* XLT - the update check's pure parts. See updparse.h. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "updparse.h"

/* ------------------------------------------------------------------ JSON */

typedef struct {
    const char *p, *end;
    int err;
} J;

static void ws(J *j)
{
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r'))
        j->p++;
}

static int eat(J *j, char c)
{
    ws(j);
    if (j->p < j->end && *j->p == c) {
        j->p++;
        return 1;
    }
    return 0;
}

static int hex4(const char *s, unsigned *v)
{
    *v = 0;
    for (int i = 0; i < 4; i++) {
        int c = s[i], d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        *v = *v << 4 | (unsigned)d;
    }
    return 0;
}

/* Appends bytes to out only if all of them fit, so UTF-8 is never cut. */
static void put(char *out, int n, int *len, const char *b, int k)
{
    if (!out || *len + k > n - 1) return;
    memcpy(out + *len, b, k);
    *len += k;
    out[*len] = 0;
}

/* As much of b as fits, cut where a UTF-8 character starts. */
static void put_cut(char *out, int n, int *len, const char *b, int k)
{
    int room = n - 1 - *len;
    if (room <= 0) return;
    if (k > room) {
        k = room;
        while (k > 0 && ((unsigned char)b[k] & 0xC0) == 0x80) k--;
    }
    memcpy(out + *len, b, k);
    *len += k;
    out[*len] = 0;
}

static void put_cp(char *out, int n, int *len, unsigned cp)
{
    char b[4];
    int k;
    if (cp < 0x80) { b[0] = (char)cp; k = 1; }
    else if (cp < 0x800) { b[0] = (char)(0xC0 | cp >> 6); b[1] = (char)(0x80 | (cp & 63)); k = 2; }
    else if (cp < 0x10000) {
        b[0] = (char)(0xE0 | cp >> 12); b[1] = (char)(0x80 | (cp >> 6 & 63)); b[2] = (char)(0x80 | (cp & 63)); k = 3;
    } else {
        b[0] = (char)(0xF0 | cp >> 18); b[1] = (char)(0x80 | (cp >> 12 & 63));
        b[2] = (char)(0x80 | (cp >> 6 & 63)); b[3] = (char)(0x80 | (cp & 63)); k = 4;
    }
    put(out, n, len, b, k);
}

/* A string value into out (cut to fit, out may be NULL to skip it). 0 or -1. */
static int jstr(J *j, char *out, int n)
{
    int len = 0;
    if (out && n > 0) out[0] = 0;
    if (!eat(j, '"')) return j->err = -1;
    while (j->p < j->end) {
        char c = *j->p++;
        if (c == '"') return 0;
        if ((unsigned char)c < 0x20) return j->err = -1;
        if (c != '\\') {
            put(out, n, &len, &c, 1);
            continue;
        }
        if (j->p >= j->end) break;
        char e = *j->p++;
        switch (e) {
        case 'n': put(out, n, &len, "\n", 1); break;
        case 't': put(out, n, &len, "\t", 1); break;
        case 'r': put(out, n, &len, "\r", 1); break;
        case 'b': case 'f': break;
        case '"': case '\\': case '/': put(out, n, &len, &e, 1); break;
        case 'u': {
            unsigned cp, lo;
            if (j->end - j->p < 4 || hex4(j->p, &cp)) return j->err = -1;
            j->p += 4;
            if (cp >= 0xD800 && cp < 0xDC00) { /* a surrogate pair */
                if (j->end - j->p >= 6 && j->p[0] == '\\' && j->p[1] == 'u' && !hex4(j->p + 2, &lo) &&
                    lo >= 0xDC00 && lo < 0xE000) {
                    j->p += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else {
                    cp = 0xFFFD;
                }
            } else if (cp >= 0xDC00 && cp < 0xE000) {
                cp = 0xFFFD;
            }
            put_cp(out, n, &len, cp);
            break;
        }
        default: return j->err = -1;
        }
    }
    return j->err = -1;
}

static int jskip(J *j, int depth);

/* A number, true, false or null: the token itself. */
static int jtoken(J *j, const char **s, int *k)
{
    ws(j);
    const char *a = j->p;
    while (j->p < j->end && (isalnum((unsigned char)*j->p) || *j->p == '-' || *j->p == '+' || *j->p == '.'))
        j->p++;
    if (j->p == a) return j->err = -1;
    *s = a;
    *k = (int)(j->p - a);
    return 0;
}

static int jskip(J *j, int depth)
{
    if (depth > 64) return j->err = -1;
    ws(j);
    if (j->p >= j->end) return j->err = -1;
    char c = *j->p;
    if (c == '"') return jstr(j, NULL, 0);
    if (c == '{' || c == '[') {
        char close = c == '{' ? '}' : ']';
        j->p++;
        if (eat(j, close)) return 0;
        do {
            if (c == '{') {
                if (jstr(j, NULL, 0) || !eat(j, ':')) return j->err = -1;
            }
            if (jskip(j, depth + 1)) return -1;
        } while (eat(j, ','));
        return eat(j, close) ? 0 : (j->err = -1);
    }
    const char *s;
    int k;
    return jtoken(j, &s, &k);
}

/* true -> 1, anything else -> 0 (skipped). */
static int jbool(J *j)
{
    ws(j);
    if (j->end - j->p >= 4 && !memcmp(j->p, "true", 4)) {
        j->p += 4;
        return 1;
    }
    jskip(j, 1);
    return 0;
}

static int64_t jint(J *j)
{
    ws(j);
    if (j->p < j->end && (*j->p == '-' || isdigit((unsigned char)*j->p))) {
        const char *s;
        int k;
        if (jtoken(j, &s, &k)) return -1;
        char b[32];
        if (k > 31) k = 31;
        memcpy(b, s, k);
        b[k] = 0;
        return strtoll(b, NULL, 10);
    }
    jskip(j, 1);
    return -1;
}

/* A string or null (null leaves out ""). */
static void jstr_or_null(J *j, char *out, int n)
{
    ws(j);
    if (j->p < j->end && *j->p == '"') jstr(j, out, n);
    else { out[0] = 0; jskip(j, 1); }
}

static int ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && !strcasecmp(s + a - b, tail);
}

static int is_hex64(const char *s)
{
    for (int i = 0; i < 64; i++)
        if (!isxdigit((unsigned char)s[i])) return 0;
    return s[64] == 0;
}

typedef struct {
    char name[96], url[512], digest[80];
    int64_t size;
} Asset;

static void parse_asset(J *j, Asset *a)
{
    memset(a, 0, sizeof(*a));
    a->size = -1;
    if (!eat(j, '{')) { jskip(j, 1); return; }
    if (eat(j, '}')) return;
    do {
        char key[32];
        if (jstr(j, key, sizeof(key)) || !eat(j, ':')) { j->err = -1; return; }
        if (!strcmp(key, "name")) jstr_or_null(j, a->name, sizeof(a->name));
        else if (!strcmp(key, "browser_download_url")) jstr_or_null(j, a->url, sizeof(a->url));
        else if (!strcmp(key, "digest")) jstr_or_null(j, a->digest, sizeof(a->digest));
        else if (!strcmp(key, "size")) a->size = jint(j);
        else jskip(j, 2);
        if (j->err) return;
    } while (eat(j, ','));
    if (!eat(j, '}')) j->err = -1;
}

static void take_asset(UpdRelease *r, const Asset *a)
{
    if (!ends_with(a->name, ".zip") || strncasecmp(a->url, "https://", 8)) return;
    int xlt = !strncasecmp(a->name, "XLT", 3);
    if (r->zip_name[0] && (!xlt || !strncasecmp(r->zip_name, "XLT", 3))) return; /* the first XLT*.zip wins */
    snprintf(r->zip_name, sizeof(r->zip_name), "%s", a->name);
    snprintf(r->zip_url, sizeof(r->zip_url), "%s", a->url);
    r->zip_size = a->size;
    r->sha256[0] = 0;
    if (!strncasecmp(a->digest, "sha256:", 7) && is_hex64(a->digest + 7)) {
        for (int i = 0; i < 64; i++) r->sha256[i] = (char)tolower((unsigned char)a->digest[7 + i]);
        r->sha256[64] = 0;
    }
}

int upd_parse_release(const char *json, int len, UpdRelease *r)
{
    memset(r, 0, sizeof(*r));
    r->zip_size = -1;
    J j = {json, json + len, 0};
    if (!eat(&j, '{')) return -1;
    if (!eat(&j, '}')) {
        do {
            char key[32];
            if (jstr(&j, key, sizeof(key)) || !eat(&j, ':')) return -1;
            if (!strcmp(key, "tag_name")) jstr_or_null(&j, r->tag, sizeof(r->tag));
            else if (!strcmp(key, "body")) jstr_or_null(&j, r->notes, sizeof(r->notes));
            else if (!strcmp(key, "draft")) r->draft = jbool(&j);
            else if (!strcmp(key, "prerelease")) r->prerelease = jbool(&j);
            else if (!strcmp(key, "assets")) {
                if (!eat(&j, '[')) { jskip(&j, 1); continue; }
                if (!eat(&j, ']')) {
                    do {
                        Asset a;
                        parse_asset(&j, &a);
                        if (j.err) return -1;
                        take_asset(r, &a);
                    } while (eat(&j, ','));
                    if (!eat(&j, ']')) return -1;
                }
            } else {
                jskip(&j, 1);
            }
            if (j.err) return -1;
        } while (eat(&j, ','));
        if (!eat(&j, '}')) return -1;
    }
    const char *v = r->tag;
    if (*v == 'v' || *v == 'V') v++;
    snprintf(r->version, sizeof(r->version), "%s", v);
    if (!isdigit((unsigned char)r->version[0]) || !r->zip_url[0]) return -1;
    return 0;
}

/* -------------------------------------------------------------- versions */

int upd_version_cmp(const char *a, const char *b)
{
    if (*a == 'v' || *a == 'V') a++;
    if (*b == 'v' || *b == 'V') b++;
    for (;;) {
        long x = 0, y = 0;
        while (isdigit((unsigned char)*a)) x = x * 10 + (*a++ - '0');
        while (isdigit((unsigned char)*b)) y = y * 10 + (*b++ - '0');
        if (x != y) return x < y ? -1 : 1;
        int ma = *a == '.', mb = *b == '.';
        if (!ma && !mb) break;
        if (ma) a++;
        if (mb) b++;
    }
    /* the numbers are equal: a suffix ("-beta") comes before the plain release */
    int sa = *a != 0, sb = *b != 0;
    if (sa != sb) return sa ? -1 : 1;
    int c = strcmp(a, b);
    return c < 0 ? -1 : c > 0;
}

/* ----------------------------------------------------------------- notes */

void upd_plain_notes(const char *md, char *out, int n)
{
    int len = 0, blank = 1; /* no blank lines at the top */
    if (n <= 0) return;
    out[0] = 0;
    while (*md) {
        const char *eol = strchr(md, '\n');
        int k = eol ? (int)(eol - md) : (int)strlen(md);
        char line[512];
        if (k > (int)sizeof(line) - 1) k = sizeof(line) - 1;
        memcpy(line, md, k);
        line[k] = 0;
        md += eol ? (eol - md) + 1 : (int)strlen(md);
        /* trim */
        int a = 0, z = (int)strlen(line);
        while (z > 0 && (line[z - 1] == '\r' || line[z - 1] == ' ' || line[z - 1] == '\t')) z--;
        line[z] = 0;
        while (line[a] == ' ' || line[a] == '\t') a++;
        char *s = line + a;
        if (!*s) {
            if (!blank) { put(out, n, &len, "\n", 1); blank = 1; }
            continue;
        }
        char o[512];
        int ol = 0;
        o[0] = 0;
        if (*s == '#') { /* a heading: just its text */
            while (*s == '#') s++;
            while (*s == ' ') s++;
        } else if ((s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ') {
            put(o, sizeof(o), &ol, "- ", 2);
            s += 2;
        }
        while (*s) {
            if ((s[0] == '*' && s[1] == '*') || (s[0] == '_' && s[1] == '_')) { s += 2; continue; }
            if (*s == '`') { s++; continue; }
            if (*s == '[') { /* [text](target) -> text */
                const char *close = strchr(s, ']');
                if (close && close[1] == '(' && strchr(close, ')')) {
                    put(o, sizeof(o), &ol, s + 1, (int)(close - s - 1));
                    s = (char *)strchr(close, ')') + 1;
                    continue;
                }
            }
            put(o, sizeof(o), &ol, s, 1);
            s++;
        }
        if (len) put(out, n, &len, "\n", 1); /* ends the line before (a blank one added its own) */
        put_cut(out, n, &len, o, ol);
        blank = 0;
    }
    while (len > 0 && out[len - 1] == '\n') out[--len] = 0;
}

/* ------------------------------------------------------------------- zip */

int upd_eboot_rank(const char *name)
{
    while (*name == '/' || *name == '\\') name++;
    if (!strcasecmp(name, "PSP/GAME/XLT/EBOOT.PBP")) return 2;
    if (!strcasecmp(name, "EBOOT.PBP") || ends_with(name, "/EBOOT.PBP")) return 1;
    return 0;
}
