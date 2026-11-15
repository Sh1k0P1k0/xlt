/* XLT - play history (<app>/cache/history.bin), keyed by file path. */
#ifndef HISTORY_H
#define HISTORY_H

#include <stdint.h>

void history_init(const char *app_dir); /* loads the file */

/* Last launch as an RTC tick (microseconds), 0 if never played. */
uint64_t history_played(const char *path);

/* Records a launch now and writes the file (called right before LoadExec). */
void history_record(const char *path);

/* Forgets every launch (the Recent rows and "Last played" start over) and deletes the file. */
void history_clear(void);
/* How many games and apps it remembers. */
int history_count(void);

/* "12 h 40 min · played yesterday", "Played today" (no time counted yet), "Never played"... */
void history_describe(const char *path, char *out, int n);

/* Seconds played in all, from xltshot's counts (0 if none). */
uint32_t history_seconds(const char *path);

/* Adds one session xltshot counted: started (RTC tick), seconds. 1 if added. */
int history_add_play(const char *path, uint64_t started, uint32_t seconds);

/* Reads SEPLUGINS/xltplay.bin (ms0:, ef0:), adds it to the history and deletes it.
 * Returns how many sessions were added. Call once at start, after history_init. */
int history_import_play(void);

/* "12 h 40 min", "45 min", "under a minute"; "" for 0. */
void history_time_text(uint32_t seconds, char *out, int n);

#endif
