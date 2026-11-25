/* Host test for src/textedit.c: which files can be edited, that saving gives back
 * the file's own bytes (BOM, CR LF) for what wasn't changed, line helpers, splice.
 * The round trip goes through the Reader's own text_to_utf8, as on the PSP. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "textedit.h"
#include "textenc.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* As the Reader loads it, then saved: the same bytes? */
static int round_trip(const char *file, int n)
{
    TedFormat f;
    const char *why;
    if (!ted_check((const uint8_t *)file, n, &f, &why)) return -1;
    int len, used;
    char *t = text_to_utf8((const uint8_t *)file, n, ENC_AUTO, &len, &used);
    int out_len;
    uint8_t *out = ted_encode(t, len, &f, &out_len);
    int same = out_len == n && !memcmp(out, file, (size_t)n);
    free(t);
    free(out);
    return same;
}

int main(void)
{
    /* the same bytes back */
    CHECK(round_trip("", 0) == 1);
    CHECK(round_trip("one\ntwo\n", 8) == 1);
    CHECK(round_trip("one\r\ntwo\r\n", 10) == 1);
    CHECK(round_trip("\xEF\xBB\xBFwith BOM\r\nΚαλημέρα\tκόσμε\r\nno final break", 46) == 1);
    CHECK(round_trip("  indented\n\n\ttab\n", 17) == 1);

    /* refused, each with its reason */
    TedFormat f;
    const char *why = NULL;
    CHECK(!ted_check((const uint8_t *)"\xFF\xFEh\0i\0", 6, &f, &why) && strstr(why, "UTF-16"));
    CHECK(!ted_check((const uint8_t *)"caf\xE9", 4, &f, &why) && strstr(why, "UTF-8"));
    CHECK(!ted_check((const uint8_t *)"a\xC0\x80", 3, &f, &why) && strstr(why, "UTF-8"));        /* overlong */
    CHECK(!ted_check((const uint8_t *)"a\xED\xA0\x80", 4, &f, &why) && strstr(why, "UTF-8"));    /* surrogate */
    CHECK(!ted_check((const uint8_t *)"cut \xE2\x82", 6, &f, &why) && strstr(why, "UTF-8"));     /* cut at the end */
    CHECK(!ted_check((const uint8_t *)"bell\a", 5, &f, &why) && strstr(why, "control"));
    CHECK(!ted_check((const uint8_t *)"old\rmac", 7, &f, &why) && strstr(why, "Mac"));
    char *big = calloc(TED_MAX + 1, 1);
    memset(big, 'a', TED_MAX + 1);
    CHECK(!ted_check((const uint8_t *)big, TED_MAX + 1, &f, &why) && strstr(why, "256 KB"));
    CHECK(ted_check((const uint8_t *)big, TED_MAX, &f, &why));
    free(big);
    /* mixed line breaks: the most common one wins */
    CHECK(ted_check((const uint8_t *)"a\r\nb\r\nc\nd", 9, &f, &why) && f.crlf == 1);
    CHECK(ted_check((const uint8_t *)"a\nb\nc\r\nd", 8, &f, &why) && f.crlf == 0);

    /* lines */
    const char *t = "ab\n\ncd\n";
    int len = 7;
    CHECK(ted_line_start(t, len, 1) == 0 && ted_line_end(t, len, 1) == 2);
    CHECK(ted_line_start(t, len, 3) == 3 && ted_line_end(t, len, 3) == 3); /* the empty line */
    CHECK(ted_line_start(t, len, 5) == 4 && ted_line_end(t, len, 4) == 6);
    CHECK(ted_line_start(t, len, 7) == 7 && ted_line_end(t, len, 7) == 7); /* after the final break */
    CHECK(ted_line_no(t, len, 0) == 1 && ted_line_no(t, len, 4) == 3 && ted_line_no(t, len, 7) == 4);
    CHECK(ted_line_count(t, len) == 4 && ted_line_count("", 0) == 1 && ted_line_count("x", 1) == 1);

    /* splice */
    int n;
    char *s = ted_splice(t, len, 0, 2, "hello", 5, &n);
    CHECK(s && n == 10 && !strcmp(s, "hello\n\ncd\n"));
    free(s);
    s = ted_splice(t, len, 7, 0, "end", 3, &n);
    CHECK(s && !strcmp(s, "ab\n\ncd\nend"));
    free(s);
    s = ted_splice(t, len, 2, 2, "", 0, &n); /* two breaks out: ab joined with cd */
    CHECK(s && !strcmp(s, "abcd\n"));
    free(s);
    CHECK(ted_splice(t, len, 6, 2, "", 0, &n) == NULL);

    /* an edit, then saved as CR LF with the BOM */
    TedFormat g = {1, 1};
    int ol;
    uint8_t *o = ted_encode("x\ny", 3, &g, &ol);
    CHECK(ol == 7 && !memcmp(o, "\xEF\xBB\xBFx\r\ny", 7));
    free(o);

    if (fails) return 1;
    printf("textedit: all passed\n");
    return 0;
}
