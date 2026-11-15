/* XLT - file names as the PSP keeps them. Portable (host-tested).
 *
 * The PSP's FAT driver gives and takes names in Shift-JIS (Microsoft's cp932),
 * not UTF-8: sceIoDread returns "α" as 83 BF, and a name passed in UTF-8 is
 * stored as Shift-JIS read wrongly ("é" becomes "ﾃｩ" on the PC). A name with a
 * character Shift-JIS lacks comes back cut off at it: the file can't be reached.
 * XLT keeps every path in the PSP's bytes and converts only to show names
 * (gfx.c reads a name that isn't valid UTF-8 as Shift-JIS) and when it makes a
 * name from text the user typed or a ZIP or web page gave (fsname_from_utf8). */
#ifndef FSNAME_H
#define FSNAME_H

#include <stdint.h>

/* One Shift-JIS character at s (at most avail bytes): its length (1 or 2) and *cp,
 * or 0 if s doesn't start with one (ASCII counts: length 1). */
int sjis_char(const unsigned char *s, int avail, uint32_t *cp);

/* A name from the PSP as UTF-8: as it is if it is valid UTF-8 already (ASCII, or a
 * name an older XLT wrote in UTF-8), else read as Shift-JIS. */
void fsname_to_utf8(const char *fs, char *out, int n);

/* UTF-8 text as a name the PSP can hold: Shift-JIS, a character it lacks written as
 * XLT Tool does (é -> e, ά -> α, – -> -, else _). *changed (if given): 1 if anything
 * had to be replaced. -1 if it doesn't fit in n bytes, else 0. */
int fsname_from_utf8(const char *utf8, char *out, int n, int *changed);

#endif
