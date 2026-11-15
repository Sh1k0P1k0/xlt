/* Host test for src/history.c's play time: xltshot's records (SEPLUGINS/xltplay.bin)
 * added at start, a cut-off record, games XLT never launched, the file deleted
 * afterwards, the old history file (padding where the seconds are now) still read,
 * and the "12 h 40 min · played yesterday" line.
 *   HOSTIO_ROOT=DIR test_history */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/history.c"

u64 g_hostio_now;
#define DAY (86400ull * 1000000ull)

static void put_records(const char *root, const XltPlayRecord *r, int n, int cut)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/ms0/SEPLUGINS/xltplay.bin", root);
    FILE *f = fopen(p, "wb");
    fwrite(r, sizeof(*r), n, f);
    if (cut) fwrite(r, 1, 100, f); /* half a record: the plugin was writing when the PSP went off */
    fclose(f);
}

int main(void)
{
    const char *root = getenv("HOSTIO_ROOT");
    char p[512];
    snprintf(p, sizeof(p), "%s/ms0", root); mkdir(p, 0777);
    snprintf(p, sizeof(p), "%s/ms0/SEPLUGINS", root); mkdir(p, 0777);
    snprintf(p, sizeof(p), "%s/ms0/XLT", root); mkdir(p, 0777);
    snprintf(p, sizeof(p), "%s/ms0/XLT/cache", root); mkdir(p, 0777);

    g_hostio_now = 1000 * DAY;
    history_init("ms0:/XLT/");
    history_record("ms0:/ISO/GAMES/Game.iso"); /* launched from XLT, played before */
    assert(history_seconds("ms0:/ISO/GAMES/Game.iso") == 0);
    char line[64];
    history_describe("ms0:/ISO/GAMES/Game.iso", line, sizeof(line));
    assert(!strcmp(line, "Played today"));

    XltPlayRecord r[3];
    memset(r, 0, sizeof(r));
    for (int i = 0; i < 3; i++) r[i].magic = XLTPLAY_MAGIC;
    r[0].seconds = 3600 * 12; r[0].started = 1000 * DAY + 1000;
    snprintf(r[0].path, sizeof(r[0].path), "ms0:/ISO/games/game.iso"); /* spelled otherwise: still that game */
    r[1].seconds = 2400; r[1].started = 1000 * DAY + 50000 * 1000000ull;
    snprintf(r[1].path, sizeof(r[1].path), "ms0:/ISO/GAMES/Game.iso");
    r[2].seconds = 90; r[2].started = 999 * DAY;
    snprintf(r[2].path, sizeof(r[2].path), "ms0:/PSP/GAME/HB/EBOOT.PBP"); /* started from the XMB */
    put_records(root, r, 3, 1);

    g_hostio_now = 1002 * DAY;
    assert(history_import_play() == 3);
    assert(history_seconds("ms0:/ISO/GAMES/Game.iso") == 12 * 3600 + 2400);
    assert(history_seconds("ms0:/PSP/GAME/HB/EBOOT.PBP") == 90);
    assert(history_played("ms0:/ISO/GAMES/Game.iso") == r[1].started + 2400ull * 1000000ull);
    history_describe("ms0:/ISO/GAMES/Game.iso", line, sizeof(line));
    assert(!strcmp(line, "12 h 40 min \xC2\xB7 played yesterday"));
    history_describe("ms0:/PSP/GAME/HB/EBOOT.PBP", line, sizeof(line));
    assert(!strcmp(line, "1 min \xC2\xB7 played 2 days ago"));
    snprintf(p, sizeof(p), "%s/ms0/SEPLUGINS/xltplay.bin", root);
    struct stat st;
    assert(stat(p, &st) != 0); /* counted once: the file is gone */
    assert(history_import_play() == 0);

    /* a record without a path (a PS1 game: the PSP doesn't say): the game XLT launched just before */
    g_hostio_now = 2000 * DAY;
    history_record("ms0:/PSP/GAME/SCES00344/EBOOT.PBP");
    XltPlayRecord p1[2];
    memset(p1, 0, sizeof(p1));
    p1[0].magic = p1[1].magic = XLTPLAY_MAGIC;
    p1[0].seconds = 95; p1[0].started = 2000 * DAY + 4000000ull;           /* 4 s after the launch */
    p1[1].seconds = 50; p1[1].started = 2000 * DAY + 600 * 1000000ull;     /* 10 min later: from the XMB */
    put_records(root, p1, 2, 0);
    assert(history_import_play() == 1);
    assert(history_seconds("ms0:/PSP/GAME/SCES00344/EBOOT.PBP") == 95);

    /* kept across a restart */
    history_init("ms0:/XLT/");
    assert(history_seconds("ms0:/ISO/GAMES/Game.iso") == 12 * 3600 + 2400);
    history_time_text(30, line, sizeof(line)); assert(!strcmp(line, "under a minute"));
    history_time_text(45 * 60, line, sizeof(line)); assert(!strcmp(line, "45 min"));
    history_time_text(150 * 3600, line, sizeof(line)); assert(!strcmp(line, "150 h"));
    history_time_text(0, line, sizeof(line)); assert(!line[0]);
    puts("history: all passed");
    return 0;
}
