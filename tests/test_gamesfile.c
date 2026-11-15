/* Host test for src/gamesfile.c: the lines of cache/games.txt XLT Tool reads. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gamesfile.h"

int main(void)
{
    Entry e;
    memset(&e, 0, sizeof(e));
    char line[512];
    e.kind = KIND_ISO;
    snprintf(e.path, sizeof(e.path), "ms0:/ISO/GAMES/God of War.iso");
    snprintf(e.id, sizeof(e.id), "UCES01401");
    snprintf(e.title, sizeof(e.title), "God of War:\tGhost\nof Sparta");
    /* 2026-11-14 12:00:00 UTC as an RTC tick */
    unsigned long long tick = (62135596800ull + 1794657600ull) * 1000000ull;
    int n = gamesfile_line(&e, tick, 45600, 1, line, sizeof(line));
    assert(n > 0 && n == (int)strlen(line));
    assert(!strcmp(line, "iso\tms0:/ISO/GAMES/God of War.iso\tUCES01401\t1794657600\t45600\t1\tGod of War: Ghost of Sparta\n"));
    e.kind = KIND_HOMEBREW;
    e.id[0] = 0;
    assert(gamesfile_line(&e, 0, 0, 0, line, sizeof(line)) > 0);
    assert(!strncmp(line, "homebrew\tms0:/ISO/GAMES/God of War.iso\t\t0\t0\t0\t", 46));
    e.kind = KIND_MUSIC; /* not a game */
    assert(gamesfile_line(&e, 0, 0, 0, line, sizeof(line)) == 0);
    puts("gamesfile: all passed");
    return 0;
}
