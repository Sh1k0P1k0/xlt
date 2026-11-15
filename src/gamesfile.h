/* XLT - cache/games.txt: the games and apps in XLT's library with their play
 * history, for XLT Tool, which reads it over USB sync. A stable, documented
 * format (unlike cache/index.bin, which is XLT's own and changes):
 *
 *   XLT games 1
 *   kind <TAB> path <TAB> id <TAB> last played <TAB> seconds played <TAB> favorite <TAB> title
 *
 * kind: iso cso zso psn ps1 homebrew. path: as XLT keeps it (the PSP's Shift-JIS
 * bytes; an EBOOT.PBP's full path for homebrew, PSN and PS1). last played: Unix
 * time (UTC seconds), 0 if never. favorite: 1 or 0. title: UTF-8. Tabs and
 * newlines never appear in the fields (they become spaces). */
#ifndef GAMESFILE_H
#define GAMESFILE_H

#include <stdint.h>
#include "entry.h"

#define GAMESFILE_HEAD "XLT games 1\n"

/* One line for a game entry (into out, n bytes): its length, 0 if e isn't a game.
 * played = RTC tick (microseconds since year 1, UTC) or 0. */
int gamesfile_line(const Entry *e, uint64_t played, uint32_t seconds, int favorite, char *out, int n);

/* Writes <app>/cache/games.txt from the given entries, with the play history. */
void gamesfile_write(const char *cache_dir, const Entry *e, int n);

#endif
