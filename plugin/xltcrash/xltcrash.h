/* xltcrash - the crash log: what catch.S saves at a crash, shared with main.c.
 *
 * One slot per crash, used once: catch.S fills it in the exception, then sends the
 * crashed thread, now in kernel mode on its kernel stack, to xltcrash_trap() in
 * main.c, which hands the slot to the log writer and puts the thread to sleep for
 * good. When every slot is used, crashes go to the handler that was there before.
 *
 * The offsets are spelled out because catch.S uses them too. */
#ifndef XLTCRASH_H
#define XLTCRASH_H

#define SLOTS       4
#define SLOT_SHIFT  8                  /* a slot is 256 bytes */

#define S_STATE     0                  /* 0 free, 1 caught (catch.S), 2 for the log, 3 logged */
#define S_THID      4                  /* the crashed thread (xltcrash_trap) */
#define S_R         8                  /* r[32]: r[0] is 0 */
#define S_STATUS    136
#define S_CAUSE     140
#define S_EPC       144
#define S_BADVADDR  148
#define S_HI        152
#define S_LO        156

#ifndef __ASSEMBLER__
typedef struct {
    volatile unsigned int state;
    int thid;
    unsigned int r[32];
    unsigned int status, cause, epc, badvaddr, hi, lo;
    unsigned int pad[(256 - 160) / 4];
} CrashSlot;

_Static_assert(sizeof(CrashSlot) == 1 << SLOT_SHIFT, "slot size");
_Static_assert(__builtin_offsetof(CrashSlot, r) == S_R, "S_R");
_Static_assert(__builtin_offsetof(CrashSlot, status) == S_STATUS, "S_STATUS");
#endif

#endif
