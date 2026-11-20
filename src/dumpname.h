/* XLT - the portable parts of copying a UMD to an ISO (umddump.c): the file's
 * name, the disc's size from its volume descriptor, and the time left.
 * Host-tested (tests/test_dumpname.c). */
#ifndef DUMPNAME_H
#define DUMPNAME_H

#include <stdint.h>

#define DUMP_NAME_MAX 120 /* bytes of the UTF-8 name, ".iso" included */

/* "Title [ID].iso" in UTF-8: characters FAT can't hold (\ / : * ? " < > |) and control
 * characters become spaces, runs of spaces one, no spaces or dots at either end; a
 * long title is cut (never inside a UTF-8 character) so the whole name fits in
 * DUMP_NAME_MAX. Without a title "[ID].iso", without either "UMD.iso". */
void dump_file_name(const char *title, const char *id, char *out, int n);

/* The sector count in a primary volume descriptor (sector 16 of the disc, 2048 bytes):
 * the both-endian volume space size, or 0 if the sector isn't one ("\1CD001"). */
uint32_t dump_pvd_sectors(const uint8_t *sector);

/* The disc's sector count from the end of umd0: (end; a real drive gives the last
 * sector's number, an emulated one the count) and the volume descriptor's size (pvd,
 * 0 = none). end_reads: sector `end` itself could be read. 0 if neither is usable. */
uint32_t dump_disc_sectors(int end, uint32_t pvd, int end_reads);

/* Time left at the speed so far: "about 12 min left", "about 1 h 5 min left",
 * "less than a minute left"; empty until there is enough to go on (5 s, 1 MB). */
void dump_time_left(uint64_t done, uint64_t total, uint32_t elapsed_ms, char *out, int n);

#endif
