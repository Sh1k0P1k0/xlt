/* xltcrash - the crash log: when a game, an app, a plugin or XLT crashes, what the
 * PSP knew at that moment is written to SEPLUGINS/xltcrash.log.
 *
 * A kernel plugin ARK loads at the "game" (games and homebrew, XLT too) and "pops"
 * runlevels, carried inside XLT and installed by it (src/crashplug.c). It registers
 * catch.S as the kernel's default exception handler: the one that runs when nothing
 * else handles an exception (a bad address, a bus error, a bad instruction...).
 *
 * At a crash, catch.S saves the registers in a slot and moves the crashed thread
 * into kernel mode the way PSPLINK does (as if it had made a system call), then
 * calls xltcrash_trap() below. That only hands the slot to the writer
 * thread and sleeps for good: the crashed thread stops where it would have
 * stopped anyway, the rest of the system goes on as before (HOME works where it
 * did). The writer formats the slot, outside the crash, and appends it to the log
 * with ordinary file calls, never the raw device (docs: no raw Memory Stick
 * access while it is mounted).
 *
 * Nothing is written while nothing crashes. Four crashes per load are logged; any
 * more go to the handler that was there before. A crash while the crashed thread
 * held the Memory Stick's driver lock can't be written (the writer would wait for
 * it): then nothing is logged, and the PSP is as stuck as it would be without us. */
#include <pspkernel.h>
#include <pspsysmem_kernel.h>
#include <psploadcore.h>
#include <pspthreadman_kernel.h>
#include <pspintrman_kernel.h>
#include <pspinit.h>
#include <psprtc.h>
#include <stddef.h>
#include <string.h>
#include <systemctrl.h>
#include "xltcrash.h"
/* from the kernel's SysclibForKernel (libpspsysclib_kernel_660), which the SDK doesn't declare */
int snprintf(char *buf, size_t n, const char *fmt, ...);
int sceKernelRegisterDefaultExceptionHandler(void *func);

PSP_MODULE_INFO("XLTCrash", PSP_MODULE_KERNEL, 1, 0);

#define LOG_MAX (64 * 1024)  /* bigger: renamed to xltcrash.old.log, a new one started */
#define TRACE_MAX 12
#define TRACE_SCAN 2048       /* bytes of the crashed thread's stack searched for return addresses */

#define NID_RTC_LOCALTIME 0xE7C27D1B /* sceRtcGetCurrentClockLocalTime */
#define NID_EXC_GETACTIVE 0xDF83875E /* sceKernelGetActiveDefaultExceptionHandler (ARK maps it to 6.60's) */

/* catch.S */
void xltcrash_handler(void);
CrashSlot s_slots[SLOTS] __attribute__((aligned(64)));
volatile unsigned int s_next;
void *s_prev;

static int (*s_localtime)(ScePspDateTime *);
static char s_dev[8] = "ms0:";
static SceUID s_ev = -1;

/* ------------------------------------------------------------ the crash */

/* The crashed thread arrives here from catch.S, in kernel mode on its kernel stack,
 * interrupts off. As PSPLINK's psplinkTrap: an interrupt handler that crashed can't be
 * put to sleep (nothing is logged, the PSP stays stuck as it would have); a thread gets
 * its interrupts back, wakes the writer and sleeps for good. */
void xltcrash_trap(CrashSlot *s)
{
    if (sceKernelIsIntrContext())
        for (;;) {}
    s->thid = sceKernelGetThreadId();
    s->state = 2;
    asm volatile("mfc0 $v0, $12\n ori $v0, $v0, 1\n mtc0 $v0, $12\n nop" ::: "v0");
    if (s_ev >= 0)
        sceKernelSetEventFlag(s_ev, 1);
    for (;;)
        sceKernelSleepThread();
}

/* ------------------------------------------------------------ the log */

static const char *const EXC[32] = {
    "interrupt", "TLB modification", "TLB load", "TLB store",
    "address error on load or instruction fetch (a bad or unaligned pointer)",
    "address error on store (a bad or unaligned pointer)",
    "bus error on instruction fetch (jumped to an address with nothing there)",
    "bus error on data load or store (an address with nothing there)",
    "syscall", "breakpoint (break instruction)", "reserved instruction (not code)",
    "coprocessor unusable", "arithmetic overflow", "trap", "", "floating point",
};

static const char *const REG[32] = {
    "zr", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra",
};

static char s_text[6144];
static int s_len;

/* appends to s_text (SysclibForKernel has snprintf but no vsnprintf, hence a macro) */
#define put(...) do { \
        if (s_len < (int)sizeof(s_text) - 1) { \
            int n_ = snprintf(s_text + s_len, sizeof(s_text) - s_len, __VA_ARGS__); \
            if (n_ > 0) s_len += n_; \
            if (s_len > (int)sizeof(s_text) - 1) s_len = sizeof(s_text) - 1; \
        } \
    } while (0)

/* the module an address is in, and where in it: "name+0x1234", or "" */
static void where(unsigned addr, char *out, int n)
{
    out[0] = 0;
    SceModule *m = sceKernelFindModuleByAddress(addr);
    if (m && addr >= m->text_addr && addr < m->text_addr + m->text_size)
        snprintf(out, n, " = %s+0x%x", m->modname, (unsigned)(addr - m->text_addr));
    else if (m)
        snprintf(out, n, " (in %s's data)", m->modname);
}

/* 1 if addr is the return address of a call: inside some module's code, with a jal or
 * jalr two instructions before it */
static int is_return(unsigned addr)
{
    if (addr & 3)
        return 0;
    SceModule *m = sceKernelFindModuleByAddress(addr);
    if (!m || addr < m->text_addr + 8 || addr >= m->text_addr + m->text_size)
        return 0;
    unsigned in = *(const volatile unsigned *)(addr - 8);
    return (in >> 26) == 3 || ((in >> 26) == 0 && (in & 0x3f) == 9);
}

static void format(const CrashSlot *s)
{
    char w[64];
    s_len = 0;
    ScePspDateTime t;
    memset(&t, 0, sizeof(t));
    if (s_localtime && s_localtime(&t) >= 0)
        put("=== crash %04d-%02d-%02d %02d:%02d:%02d ===\n", t.year, t.month, t.day, t.hour, t.minute, t.second);
    else
        put("=== crash at %u ms since start ===\n", (unsigned)(sceKernelGetSystemTimeLow() / 1000));
    const char *file = sceKernelInitFileName();
    if (file && file[0]) /* (empty for some launches, XLT's among them: the module names below say it) */
        put("running: %s (apitype 0x%x)\n", file, sceKernelInitApitype());
    else
        put("running: apitype 0x%x\n", sceKernelInitApitype());

    SceKernelThreadInfo ti;
    memset(&ti, 0, sizeof(ti));
    ti.size = sizeof(ti);
    int have_ti = sceKernelReferThreadStatus(s->thid, &ti) >= 0;
    put("thread: %s (0x%08x, %s)\n", have_ti ? ti.name : "?", (unsigned)s->thid,
        !have_ti ? "?" : (ti.attr & PSP_THREAD_ATTR_USER) ? "user" : "kernel");

    unsigned code = (s->cause >> 2) & 31;
    put("what: %s (exception %u)\n", EXC[code][0] ? EXC[code] : "unknown", code);
    where(s->epc, w, sizeof(w));
    put("at: 0x%08x%s%s\n", s->epc, w, (s->cause & 0x80000000u) ? " (the branch before it: the fault is in its delay slot)" : "");
    if (code == 4 || code == 5) /* (the CPU only sets it for address errors, not bus errors) */
        put("bad address: 0x%08x\n", s->badvaddr);
    where(s->r[31], w, sizeof(w));
    put("called from (ra): 0x%08x%s\n", s->r[31], w);

    for (int i = 0; i < 32; i += 4)
        put("%s %08x  %s %08x  %s %08x  %s %08x\n", REG[i], s->r[i], REG[i + 1], s->r[i + 1],
            REG[i + 2], s->r[i + 2], REG[i + 3], s->r[i + 3]);
    put("hi %08x  lo %08x  status %08x  cause %08x\n", s->hi, s->lo, s->status, s->cause);

    /* the calls under it: return addresses found on its stack (the stack's own range only;
     * old values left on it can show up too) */
    unsigned sp = s->r[29], lo = 0, hi = 0;
    if (have_ti && ti.stack && ti.stackSize > 0) {
        lo = (unsigned)ti.stack;
        hi = lo + (unsigned)ti.stackSize;
    }
    if (sp >= lo && sp < hi && !(sp & 3)) {
        put("calls on the stack (maybe also older ones):\n");
        unsigned end = sp + TRACE_SCAN < hi ? sp + TRACE_SCAN : hi;
        int found = 0;
        for (unsigned a = sp; a < end && found < TRACE_MAX; a += 4) {
            unsigned v = *(const volatile unsigned *)a;
            if (is_return(v)) {
                where(v, w, sizeof(w));
                put("  0x%08x%s\n", v, w);
                found++;
            }
        }
        if (!found)
            put("  none found\n");
    } else {
        put("stack: sp 0x%08x is outside the thread's stack (0x%08x-0x%08x): not searched\n", sp, lo, hi);
    }
    put("\n");
}

static void write_log(void)
{
    char p[64], old[64];
    snprintf(p, sizeof(p), "%sSEPLUGINS/xltcrash.log", s_dev);
    snprintf(old, sizeof(old), "%sSEPLUGINS/xltcrash.old.log", s_dev);
    SceIoStat st;
    if (sceIoGetstat(p, &st) >= 0 && st.st_size > LOG_MAX) {
        sceIoRemove(old);
        sceIoRename(p, old);
    }
    SceUID fd = sceIoOpen(p, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd < 0)
        return;
    sceIoWrite(fd, s_text, s_len);
    sceIoClose(fd);
}

static int writer(SceSize args, void *argp)
{
    for (;;) {
        u32 bits = 0;
        if (sceKernelWaitEventFlag(s_ev, 1, PSP_EVENT_WAITOR | PSP_EVENT_WAITCLEAR, &bits, NULL) < 0)
            return 0;
        for (int i = 0; i < SLOTS; i++)
            if (s_slots[i].state == 2) {
                format(&s_slots[i]);
                write_log();
                s_slots[i].state = 3;
            }
    }
}

/* ------------------------------------------------------------ start */

int module_start(SceSize args, void *argp)
{
    SceUID d = sceIoDopen("ms0:/");
    if (d >= 0) sceIoDclose(d);
    else snprintf(s_dev, sizeof(s_dev), "ef0:"); /* a PSP Go without a card */
    s_localtime = (void *)sctrlHENFindFunction("sceRTC_Service", "sceRtc_driver", NID_RTC_LOCALTIME);
    if (!s_localtime)
        s_localtime = (void *)sctrlHENFindFunction("sceRTC_Service", "sceRtc", NID_RTC_LOCALTIME);

    s_ev = sceKernelCreateEventFlag("xltcrash", 0, 0, NULL);
    SceUID th = s_ev >= 0 ? sceKernelCreateThread("xltcrash", writer, 0x18, 0x2000, 0, NULL) : -1;
    if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0)
        return 0; /* no writer: nothing registered, the PSP's own handling stays */

    int (*get_active)(void) = (void *)sctrlHENFindFunction("sceExceptionManager", "ExceptionManagerForKernel", NID_EXC_GETACTIVE);
    s_prev = get_active ? (void *)get_active() : NULL;
    sceKernelDcacheWritebackAll();
    int r = sceKernelRegisterDefaultExceptionHandler((void *)((unsigned)xltcrash_handler | 0x80000000u));
    if (r < 0) { /* the only thing written at start: crashes won't be logged, and why */
        s_len = 0;
        put("xltcrash: couldn't register the crash handler (%08x) for %s\n\n", (unsigned)r,
            sceKernelInitFileName() ? sceKernelInitFileName() : "?");
        write_log();
    }
    return 0;
}
