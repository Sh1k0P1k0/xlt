/* XLT - copies the UMD in the drive to an ISO on the Memory Stick
 * (ms0:/ISO/Title [ID].iso), on a thread of its own, with progress and stop.
 *
 * The disc is read raw through umd0:, whose reads and seeks count 2048-byte
 * sectors (as ARK's Inferno driver emulates it); the end of umd0: is the
 * disc's size, with the volume descriptor's size as the fallback. The copy goes
 * to "<name>.xltpart" and is renamed only when every sector is in: a stopped or
 * failed copy is deleted, never left as a broken ISO. A sector that still can't
 * be read after a few tries stops the copy (no zero-filled sectors). */
#ifndef UMDDUMP_H
#define UMDDUMP_H

#include <stdint.h>
#include "entry.h"

enum { DUMP_IDLE, DUMP_PREPARING, DUMP_COPYING, DUMP_DONE, DUMP_FAILED, DUMP_STOPPED };

typedef struct {
    int state;
    uint64_t done, total;  /* bytes */
    uint32_t elapsed_ms;   /* since the copying began */
    char name[128];        /* the ISO's name, UTF-8 (for the screen) */
    char error[128];       /* DUMP_FAILED: what went wrong, for the user */
} DumpStatus;

/* Starts copying disc (the UMD entry) into ms0:/ISO/. 0, or -1 with *why. */
int umddump_start(const Entry *disc, const char **why);
void umddump_status(DumpStatus *out);
int umddump_busy(void);       /* preparing or copying */
void umddump_stop(void);      /* asks the thread to stop (the part is deleted) */
void umddump_abort(void);     /* stops and waits for it: before XLT exits or starts a game */
void umddump_on_suspend(void); /* power callback: the PSP is going to sleep */
/* After DONE / FAILED / STOPPED: back to IDLE (the thread is gone). */
void umddump_clear(void);

#endif
