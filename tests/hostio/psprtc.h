#ifndef HOSTIO_PSPRTC_H
#define HOSTIO_PSPRTC_H
/* Host stand-in: the clock is whatever the test sets. */
typedef unsigned long long u64;
extern u64 g_hostio_now;
static inline int sceRtcGetCurrentTick(u64 *t) { *t = g_hostio_now; return 0; }
#endif
