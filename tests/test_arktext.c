/* Host test for SETTINGS.TXT clock editing. */
#include <stdio.h>
#include <string.h>
#include "../src/ark.h"
#include "../src/arktext.h"

static int fails;
#define CHECK(cond, msg) do { if (cond) printf("OK   %s\n", msg); else { printf("FAIL %s\n", msg); fails++; } } while (0)

static const char *SAMPLE =
    "always, usbcharge, on\r\n"
    "always, cpuclock:333, on\r\n"
    "# comment, with, commas\r\n"
    "\r\n"
    "ULUS10041, cpuclock:222, on\r\n"
    "ULUS10041, highmem, on\r\n"
    "NPUH10001, overclock, on\r\n";

int main(void)
{
    char out[4096], out2[4096];

    CHECK(arktext_get_clock(SAMPLE, NULL) == CLOCK_333, "global game clock read from 'always'");
    CHECK(arktext_get_clock(SAMPLE, "ULUS10041") == CLOCK_222, "per-game clock read");
    CHECK(arktext_get_clock(SAMPLE, "NPUH10001") == CLOCK_333, "ARK-4 style key read");
    CHECK(arktext_get_clock(SAMPLE, "ULES00000") == CLOCK_AUTO, "unknown game is Auto");

    int n = arktext_set_clock(SAMPLE, "ULUS10041", CLOCK_333, 0, out, sizeof(out));
    out[n > 0 ? n : 0] = 0;
    CHECK(n > 0 && arktext_get_clock(out, "ULUS10041") == CLOCK_333, "per-game set to 333");
    CHECK(strstr(out, "ULUS10041, highmem, on\n") != NULL, "other per-game line kept");
    CHECK(strstr(out, "# comment, with, commas\n") != NULL, "comment kept");
    CHECK(strstr(out, "always, cpuclock:333, on\n") != NULL, "global line untouched by per-game edit");
    CHECK(strstr(out, "ULUS10041, cpuclock:222") == NULL, "old per-game clock removed");

    n = arktext_set_clock(out, "ULUS10041", CLOCK_AUTO, 0, out2, sizeof(out2));
    out2[n > 0 ? n : 0] = 0;
    CHECK(n > 0 && strstr(out2, "ULUS10041, cpuclock") == NULL, "Auto removes the per-game line");
    CHECK(arktext_get_clock(out2, "ULUS10041") == CLOCK_AUTO, "per-game now Auto");

    n = arktext_set_clock(SAMPLE, NULL, CLOCK_222, 0, out, sizeof(out));
    out[n > 0 ? n : 0] = 0;
    CHECK(strstr(out, "vsh, cpuclock:333, on\n") != NULL, "'always' clock kept for the XMB only");
    CHECK(strstr(out, "game, cpuclock:222, on\n") != NULL, "game default line added");
    CHECK(arktext_get_clock(out, NULL) == CLOCK_222, "global game clock now 222");
    CHECK(strstr(out, "always, usbcharge, on\n") != NULL, "non-clock always line kept");

    n = arktext_set_clock("", "ULJM05500", CLOCK_133, 1, out, sizeof(out));
    out[n > 0 ? n : 0] = 0;
    CHECK(!strcmp(out, "ULJM05500, powersave, on\n"), "ARK-4 key written for empty file");

    CHECK(arktext_set_clock(SAMPLE, NULL, CLOCK_222, 0, out, 20) == -1, "too small buffer reports error");

    /* XLT's own clock line (arktext_free_clock): the user's file after the 2026-11-23 clean install */
    {
        static const char *XLT = "ms0:/PSP/GAME/XLT/EBOOT.PBP";
        static const char *USER = "always, usbcharge, on\ngame, cpuclock:333, on\r\nalways, wpa2, on\n";
        char o[1024], o2[1024];
        n = arktext_free_clock(USER, XLT, 0, o, sizeof(o));
        o[n > 0 ? n : 0] = 0;
        CHECK(n > 0, "'game, cpuclock:333, on' locks XLT: line added");
        CHECK(!strcmp(o, "always, usbcharge, on\ngame, cpuclock:333, on\nalways, wpa2, on\n"
                         "ms0:/PSP/GAME/XLT/EBOOT.PBP, cpuclock:333, off\n"), "other lines kept, XLT's off line (same speed) last");
        CHECK(arktext_get_clock(o, NULL) == CLOCK_333, "games keep their 333");
        CHECK(arktext_free_clock(o, XLT, 0, o2, sizeof(o2)) == 0, "already free: nothing to write");
        CHECK(arktext_free_clock(o, "MS0:/psp/game/xlt/eboot.pbp", 0, o2, sizeof(o2)) == 0, "path compared without case");

        /* Settings sets the all-games clock: its new game line lands after XLT's */
        n = arktext_set_clock(o, NULL, CLOCK_222, 0, o2, sizeof(o2));
        o2[n > 0 ? n : 0] = 0;
        n = arktext_free_clock(o2, XLT, 0, o, sizeof(o));
        o[n > 0 ? n : 0] = 0;
        CHECK(n > 0, "a game line after XLT's locks it again");
        const char *g = strstr(o, "game, cpuclock:222, on\n"), *x = strstr(o, XLT);
        CHECK(g && x && x > g && !strstr(x + 1, XLT), "XLT's line moved after it, once");

        CHECK(arktext_free_clock("always, usbcharge, on\nULUS10041, cpuclock:333, on\nvsh, cpuclock:333, on\n", XLT, 0,
                                 o, sizeof(o)) == 0, "per-game and XMB clocks don't lock XLT");
        CHECK(arktext_free_clock("", XLT, 0, o, sizeof(o)) == 0, "empty file: nothing to do");
        CHECK(arktext_free_clock("game, cpuclock:333, off\n", XLT, 0, o, sizeof(o)) == 0, "an off game line locks nothing");
        n = arktext_free_clock("always, overclock, on\n", XLT, 1, o, sizeof(o));
        o[n > 0 ? n : 0] = 0;
        CHECK(n > 0 && strstr(o, "EBOOT.PBP, overclock, off\n"), "ARK-4 key kept for the off line");

        /* POWER2's line: 133 off doesn't clear a 333 lock (ARK-5 clears only its own speed) */
        static const char *P2 = "game, cpuclock:333, on\nms0:/PSP/GAME/XLT/EBOOT.PBP, cpuclock:133, off\n";
        n = arktext_free_clock(P2, XLT, 0, o, sizeof(o));
        o[n > 0 ? n : 0] = 0;
        CHECK(n > 0 && !strcmp(o, "game, cpuclock:333, on\nms0:/PSP/GAME/XLT/EBOOT.PBP, cpuclock:333, off\n"),
              "a 133 off line under a 333 lock is replaced by a 333 one");
        CHECK(arktext_free_clock(o, XLT, 0, o2, sizeof(o2)) == 0, "then free");

        /* two speeds set along the way: an off line for each */
        n = arktext_free_clock("always, cpuclock:222, on\ngame, cpuclock:333, on\n", XLT, 0, o, sizeof(o));
        o[n > 0 ? n : 0] = 0;
        CHECK(n > 0 && strstr(o, "EBOOT.PBP, cpuclock:222, off\n") && strstr(o, "EBOOT.PBP, cpuclock:333, off\n"),
              "off lines for both speeds");
        CHECK(arktext_free_clock("game, cpuclock:333, on\ngame, cpuclock:222, off\n", XLT, 0, o, sizeof(o)) > 0,
              "a 222 off doesn't clear 333 (still locked)");
        CHECK(arktext_free_clock("game, cpuclock:333, on\ngame, cpuclock:333, off\n", XLT, 0, o, sizeof(o)) == 0,
              "a same-speed off clears it");
        CHECK(arktext_free_clock(USER, XLT, 0, o, 30) == -1, "too small buffer reports error");
        CHECK(arktext_free_clock(USER, "ms0:/a,b/EBOOT.PBP", 0, o, sizeof(o)) == -1, "a comma in the path is refused");
    }

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
