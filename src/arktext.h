#ifndef ARKTEXT_H
#define ARKTEXT_H

/* Clock that applies to `id` (NULL = all games) in SETTINGS.TXT text. */
int arktext_get_clock(const char *text, const char *id);

/* Writes `text` with the clock lines for `id` (NULL = "game") replaced by
 * `clock` (CLOCK_AUTO removes them) into out. Returns length or -1. */
int arktext_set_clock(const char *text, const char *id, int clock, int ark4, char *out, int cap);

/* XLT sets its own clock (system.c), which only works when no ARK clock line applies
 * to it: "game, cpuclock:333, on" also covers homebrew. When one does, writes `text`
 * with "<path>, cpuclock:333, off" (an off line for each speed that covers it: ARK-5
 * clears a clock only with an off line of the same speed) as the last clock lines
 * for `path` into out. Returns the length, 0 when nothing locks the clock (out
 * untouched), -1 on error or when the result would still be locked. */
int arktext_free_clock(const char *text, const char *path, int ark4, char *out, int cap);

#endif
