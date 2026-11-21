/* XLT - copies the UMD to an ISO. See umddump.h. */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <psppower.h>
#include <pspumd.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include "umddump.h"
#include "dumpname.h"
#include "fileops.h"
#include "fsname.h"
#include "library.h"
#include "sysctl.h"
#include "ui_internal.h"

#define SECTOR 2048
#define CHUNK_SECTORS 128         /* 256 KB a read */
#define TRIES 4                   /* reads of one chunk before going sector by sector */
#define ISO_DIR "ms0:/ISO"
#define PART_EXT ".xltpart"
#define SPARE (4u << 20)          /* free space kept on top of the ISO */

static SceUID s_thread = -1;
static volatile int s_stop, s_slept;
static DumpStatus s_st;           /* written by the thread; the UI copies it */
static Entry s_disc;
static char s_final[FOP_PATH + 16], s_part[FOP_PATH + 32];

static void note(const char *fmt, const char *a, uint64_t b, uint64_t c)
{
    char msg[200];
    snprintf(msg, sizeof(msg), fmt, a, (unsigned long long)b, (unsigned long long)c);
    library_note(msg);
}

static int exists(const char *p)
{
    SceIoStat st;
    return sceIoGetstat(p, &st) >= 0;
}

static void fail(const char *msg)
{
    snprintf(s_st.error, sizeof(s_st.error), "%s", msg);
}

/* The part file goes, even right after a wake-up, when the stick needs a moment. */
static void remove_part(void)
{
    for (int i = 0; i < 25 && exists(s_part); i++) {
        if (sceIoRemove(s_part) >= 0)
            break;
        sceKernelDelayThread(200 * 1000);
    }
}

/* n sectors at lba into buf: retried, then one sector at a time for the last try.
 * Returns the sectors read (n), or -1. */
static int read_sectors(SceUID fd, uint8_t *buf, uint32_t lba, int n)
{
    for (int t = 0; t < TRIES && !s_stop; t++) {
        if (sceIoLseek32(fd, (int)lba, PSP_SEEK_SET) == (int)lba && sceIoRead(fd, buf, n) == n)
            return n;
        if (sceUmdCheckMedium() <= 0)
            return -1;
        sceKernelDelayThread(150 * 1000 * (t + 1));
    }
    for (int i = 0; i < n && !s_stop; i++) { /* find the sector that won't read */
        int ok = 0;
        for (int t = 0; t < TRIES && !ok; t++)
            ok = sceIoLseek32(fd, (int)(lba + i), PSP_SEEK_SET) == (int)(lba + i) &&
                 sceIoRead(fd, buf + (uint32_t)i * SECTOR, 1) == 1;
        if (!ok)
            return -1;
    }
    return s_stop ? -1 : n;
}

static int dump(void)
{
    SceUID in = sceIoOpen("umd0:", PSP_O_RDONLY, 0);
    if (in < 0) {
        fail("The disc can't be read.");
        note("umd dump: can't open umd0: (%s%llx)", "", (uint32_t)in, 0);
        return -1;
    }
    uint8_t *buf = memalign(64, CHUNK_SECTORS * SECTOR);
    if (!buf) {
        sceIoClose(in);
        fail("Not enough memory to copy the disc.");
        return -1;
    }
    int r = -1;
    SceUID out = -1;
    /* The size. On a real drive the end of umd0: is the LAST sector's number, one
     * less than the count (seen on a PSP-E1004: end 247023, volume descriptor
     * 247024); ARK's Inferno, emulating it, gives the count. So the volume
     * descriptor's size wins when the two are that close; otherwise end + 1 if that
     * sector reads, else end. */
    int end = sceIoLseek32(in, 0, PSP_SEEK_END);
    uint32_t pvd = read_sectors(in, buf, 16, 1) == 1 ? dump_pvd_sectors(buf) : 0;
    uint32_t sectors = dump_disc_sectors(end, pvd, end > 16 && read_sectors(in, buf, (uint32_t)end, 1) == 1);
    note("umd dump: %s, umd0: end %llu sectors, volume descriptor %llu", s_disc.id, (uint32_t)end, pvd);
    note("umd dump: %scopying %llu sectors (%llu MB)", "", sectors, (uint64_t)sectors * SECTOR >> 20);
    if (sectors <= 16) {
        fail("The disc's size can't be read.");
        goto done;
    }
    s_st.total = (uint64_t)sectors * SECTOR;

    /* Room for it? The PSP driver's own count: it holds the PSP ~12 s the first time
     * after XLT starts, then answers at once. Never the raw partition (sysctl.c). The
     * dialog says so first (pause_s), drawn before the PSP stops. */
    s_st.pause_s = sysctl_storage_pause_s();
    if (s_st.pause_s > 0)
        sceKernelDelayThread(200 * 1000);
    int counted = sysctl_storage_measure() >= 0;
    s_st.pause_s = 0;
    if (counted) {
        uint64_t fr, tot;
        if (sysctl_storage("ms0:", &fr, &tot) == 0 && fr < s_st.total + SPARE) {
            char need[16], have[16];
            format_size(need, sizeof(need), s_st.total);
            format_size(have, sizeof(have), fr);
            snprintf(s_st.error, sizeof(s_st.error), "Not enough space: the disc needs %s, the Memory Stick has %s free.",
                     need, have);
            goto done;
        }
    }

    /* The name, as the PSP keeps names, and one not taken yet. */
    char utf[DUMP_NAME_MAX + 8], fsn[FOP_PATH];
    dump_file_name(s_disc.title, s_disc.id, utf, sizeof(utf));
    if (fsname_from_utf8(utf, fsn, sizeof(fsn) - 16, NULL) != 0)
        dump_file_name("", s_disc.id, fsn, sizeof(fsn) - 16); /* the ID alone is plain ASCII */
    sceIoMkdir(ISO_DIR, 0777);
    char uniq[FOP_PATH];
    fop_unique_name(ISO_DIR, fsn, uniq, sizeof(uniq) - 32, exists);
    snprintf(s_final, sizeof(s_final), "%s/%s", ISO_DIR, uniq);
    snprintf(s_part, sizeof(s_part), "%s" PART_EXT, s_final);
    fsname_to_utf8(uniq, s_st.name, sizeof(s_st.name));
    out = sceIoOpen(s_part, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (out < 0) {
        fail("The ISO can't be made in the ISO folder.");
        goto done;
    }

    s_st.state = DUMP_COPYING;
    uint32_t t0 = sceKernelGetSystemTimeLow();
    for (uint32_t lba = 0; lba < sectors && !s_stop;) {
        int n = sectors - lba < CHUNK_SECTORS ? (int)(sectors - lba) : CHUNK_SECTORS;
        if (read_sectors(in, buf, lba, n) != n) {
            if (s_stop) break;
            if (s_slept) fail("The PSP went to sleep, so the copy stopped.");
            else if (sceUmdCheckMedium() <= 0) fail("The disc was taken out.");
            else {
                snprintf(s_st.error, sizeof(s_st.error),
                         "A part of the disc can't be read (at %u MB). Clean the disc and try again.",
                         (unsigned)((uint64_t)lba * SECTOR >> 20));
                note("umd dump: unreadable around sector %s%llu of %llu", "", lba, sectors);
            }
            goto done;
        }
        int bytes = n * SECTOR;
        if (sceIoWrite(out, buf, bytes) != bytes) {
            fail(s_slept ? "The PSP went to sleep, so the copy stopped." : "The Memory Stick is full, or can't be written to.");
            goto done;
        }
        lba += n;
        s_st.done = (uint64_t)lba * SECTOR;
        s_st.elapsed_ms = (sceKernelGetSystemTimeLow() - t0) / 1000;
        scePowerTick(PSP_POWER_TICK_SUSPEND); /* no auto-sleep in the middle (the screen may still dim) */
    }
    if (s_stop)
        goto done;
    sceIoClose(out);
    out = -1;
    SceIoStat st;
    if (sceIoGetstat(s_part, &st) < 0 || (uint64_t)st.st_size != s_st.total) {
        fail("The copy came out the wrong size.");
        goto done;
    }
    if (sceIoRename(s_part, s_final) < 0) {
        fail("The finished ISO couldn't be renamed.");
        goto done;
    }
    r = 0;
    {
        uint32_t ms = s_st.elapsed_ms ? s_st.elapsed_ms : 1;
        note("umd dump: done, %s, %llu MB in %llu s", s_st.name, s_st.total >> 20, ms / 1000);
    }
done:
    if (out >= 0)
        sceIoClose(out);
    sceIoClose(in);
    free(buf);
    if (r != 0 && s_part[0]) {
        remove_part();
        if (s_stop) note("umd dump: stopped at %s%llu of %llu MB", "", s_st.done >> 20, s_st.total >> 20);
        else note("umd dump: failed: %s (%llu, %llu)", s_st.error, s_st.done >> 20, s_st.total >> 20);
    }
    return r;
}

static int dump_thread(SceSize args, void *argp)
{
    int r = dump();
    s_st.state = r == 0 ? DUMP_DONE : s_stop ? DUMP_STOPPED : DUMP_FAILED;
    return 0;
}

int umddump_start(const Entry *disc, const char **why)
{
    if (s_thread >= 0) {
        *why = "A disc is being copied already";
        return -1;
    }
    memset(&s_st, 0, sizeof(s_st));
    s_disc = *disc;
    s_final[0] = s_part[0] = 0;
    s_stop = s_slept = 0;
    s_st.state = DUMP_PREPARING;
    s_thread = sceKernelCreateThread("xl_dump", dump_thread, 0x30, 0x4000, PSP_THREAD_ATTR_USER, NULL);
    if (s_thread < 0 || sceKernelStartThread(s_thread, 0, NULL) < 0) {
        if (s_thread >= 0) sceKernelDeleteThread(s_thread);
        s_thread = -1;
        s_st.state = DUMP_IDLE;
        *why = "The copy couldn't be started";
        return -1;
    }
    return 0;
}

void umddump_status(DumpStatus *out) { *out = s_st; }

int umddump_busy(void) { return s_st.state == DUMP_PREPARING || s_st.state == DUMP_COPYING; }

void umddump_stop(void) { s_stop = 1; }

void umddump_on_suspend(void) { s_slept = 1; }

static void join(void)
{
    if (s_thread < 0)
        return;
    SceUInt timeout = 8 * 1000 * 1000; /* a retried read and the part's removal */
    sceKernelWaitThreadEnd(s_thread, &timeout);
    sceKernelDeleteThread(s_thread);
    s_thread = -1;
}

void umddump_abort(void)
{
    if (s_thread < 0)
        return;
    s_stop = 1;
    join();
}

void umddump_clear(void)
{
    if (umddump_busy())
        return;
    join();
    s_st.state = DUMP_IDLE;
}
