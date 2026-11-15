/* XLT - USB sync. See usbsync.h and plugin/xltsync/main.c. */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspusb.h>
#include <kubridge.h>
#include <systemctrl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "usbsync.h"
#include "sysctl.h"
#include "ark.h"
#include "library.h"
#include "ui.h"
#include "xltsync.h"      /* plugin/xltsync: the protocol's names */
#include "xltsync_data.h" /* the module, built by plugin/xltsync/Makefile */

#define POLL_FRAMES 10           /* the module's state, six times a second */
#define SETTLE_US 1500000        /* changes, then this long without more: rescan */
#define NUDGE_US 3000000         /* the cable in this long and the PC hasn't set it up: nudge */

static char s_dir[256] = "ms0:/PSP/GAME/XLT/";
static int s_on;
static SceUID s_mod = -1;
static void *s_status_fn;
static char s_err[64];
static XltsStatus s_st;
static int s_frames;
static unsigned s_changes_seen;
static int s_pending;            /* changes not in the library yet */
static SceInt64 s_last_change, s_cable_since;
static int s_nudged, s_hinted;
static volatile int s_resumed;

/* a line in XLT's log (cache/scan.log), with the same ms clock as xltsync.log */
static void note(const char *fmt, ...)
{
    char text[160], line[180];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    snprintf(line, sizeof(line), "%u USB sync: %s", sceKernelGetSystemTimeLow() / 1000, text);
    library_note(line);
}

void usbsync_init(const char *dir) { snprintf(s_dir, sizeof(s_dir), "%s", dir); }
int usbsync_active(void) { return s_on; }
int usbsync_attached(void) { return s_on && s_st.attached; }
int usbsync_talking(void) { return s_on && s_st.talking; }
const char *usbsync_error(void) { return s_err; }
void usbsync_on_resume(void) { s_resumed = 1; }

/* the module file in XLT's folder: written when missing or not the one this XLT carries */
static int install(const char *path)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd >= 0) {
        int same = sceIoLseek32(fd, 0, PSP_SEEK_END) == (int)sizeof(xltsync_prx);
        sceIoLseek32(fd, 0, PSP_SEEK_SET);
        static unsigned char buf[4096];
        for (int at = 0; same && at < (int)sizeof(xltsync_prx); at += sizeof(buf)) {
            int n = sizeof(xltsync_prx) - at < sizeof(buf) ? (int)sizeof(xltsync_prx) - at : (int)sizeof(buf);
            same = sceIoRead(fd, buf, n) == n && !memcmp(buf, xltsync_prx + at, n);
        }
        sceIoClose(fd);
        if (same)
            return 0;
    }
    fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    int ok = fd >= 0 && sceIoWrite(fd, xltsync_prx, sizeof(xltsync_prx)) == (int)sizeof(xltsync_prx);
    if (fd >= 0)
        sceIoClose(fd);
    if (!ok)
        sceIoRemove(path);
    return ok ? 0 : -1;
}

static int read_status(void)
{
    if (!s_status_fn)
        return -1;
    static XltsStatus st __attribute__((aligned(64)));
    KernelCallArg a;
    memset(&a, 0, sizeof(a));
    a.arg1 = (u32)&st;
    if (kuKernelCall(s_status_fn, &a) < 0)
        return -1;
    s_st = st;
    return 0;
}

int usbsync_start(void)
{
    s_err[0] = 0;
    if (s_on)
        return 0;
    if (!ark_version()) {
        snprintf(s_err, sizeof(s_err), "USB sync needs ARK");
        return -1;
    }
    if (sysctl_usb_active()) {
        snprintf(s_err, sizeof(s_err), "Turn USB mode off first");
        return -1;
    }
    if (sysctl_usb_load() < 0) {
        snprintf(s_err, sizeof(s_err), "Couldn't load the system's USB driver");
        return -1;
    }
    if (s_mod < 0) {
        char prx[300], log[300];
        snprintf(prx, sizeof(prx), "%sxltsync.prx", s_dir);
        snprintf(log, sizeof(log), "%sxltsync.log", s_dir);
        if (install(prx) < 0) {
            snprintf(s_err, sizeof(s_err), "Couldn't write xltsync.prx");
            return -1;
        }
        SceUID mid = kuKernelLoadModule(prx, 0, NULL);
        int st = 0;
        int r = mid < 0 ? mid : sceKernelStartModule(mid, strlen(log) + 1, log, &st, NULL);
        if (mid < 0 || r < 0 || st != 0) {
            snprintf(s_err, sizeof(s_err), "xltsync.prx: load %08x start %08x/%d", (unsigned)mid, (unsigned)r, st);
            if (mid >= 0)
                sceKernelUnloadModule(mid);
            return -1;
        }
        s_mod = mid;
        s_status_fn = (void *)sctrlHENFindFunction("XLTSync", XLTSYNC_LIB, XLTSYNC_NID_STATUS);
    }
    sysctl_usb_prepare(); /* a PSP Go's USB charging can block the connection */
    if (sceUsbGetState() & PSP_USB_ACTIVATED) {
        sceUsbDeactivate(XLTSYNC_PID);
        sceKernelDelayThread(10000);
    }
    int r = sceUsbStart(PSP_USBBUS_DRIVERNAME, 0, 0);
    int r2 = sceUsbStart(XLTSYNC_DRIVER, 0, 0);
    if (r2 < 0) {
        sceUsbStop(PSP_USBBUS_DRIVERNAME, 0, 0);
        snprintf(s_err, sizeof(s_err), "USB start: bus %08x sync %08x", (unsigned)r, (unsigned)r2);
        return -1;
    }
    r = sceUsbActivate(XLTSYNC_PID);
    if (r < 0) {
        sceUsbStop(XLTSYNC_DRIVER, 0, 0);
        sceUsbStop(PSP_USBBUS_DRIVERNAME, 0, 0);
        snprintf(s_err, sizeof(s_err), "USB activate: %08x", (unsigned)r);
        return -1;
    }
    s_on = 1;
    note(s_status_fn ? "on (cable %s)" : "on, but its status function wasn't found (cable %s)",
         (sceUsbGetState() & PSP_USB_CABLE_CONNECTED) ? "in" : "out");
    s_frames = POLL_FRAMES; /* the first poll reads at once */
    s_cable_since = 0;
    s_nudged = s_hinted = 0;
    if (read_status() == 0)
        s_changes_seen = s_st.changes; /* what was before this start is in the library already */
    return 0;
}

void usbsync_stop(void)
{
    if (!s_on)
        return;
    sceUsbDeactivate(XLTSYNC_PID);
    sceUsbStop(XLTSYNC_DRIVER, 0, 0);
    sceUsbStop(PSP_USBBUS_DRIVERNAME, 0, 0);
    s_on = 0;
    memset(&s_st, 0, sizeof(s_st));
    /* the module stays loaded: starting again only restarts the driver */
    if (s_pending && !library_scanning()) { /* what came in last still gets into the library */
        library_rescan();
        s_pending = 0;
    }
}

int usbsync_progress(char *out, int n)
{
    if (!s_on || !s_st.busy || !s_st.total)
        return 0;
    snprintf(out, n, "From PC %u%%", (unsigned)((unsigned long long)s_st.done * 100 / s_st.total));
    return 1;
}

int usbsync_poll(void)
{
    if (s_resumed) { /* sleep reset the USB port: start over */
        s_resumed = 0;
        if (s_on) {
            usbsync_stop();
            usbsync_start();
        }
    }
    if (!s_on || ++s_frames < POLL_FRAMES)
        return 0;
    s_frames = 0;
    XltsStatus old = s_st;
    if (read_status() < 0)
        return 0;
    SceInt64 now = sceKernelGetSystemTimeWide();
    if (old.attached != s_st.attached || old.talking != s_st.talking)
        note("attached %u, talking %u", s_st.attached, s_st.talking);
    int changed = old.attached != s_st.attached || old.talking != s_st.talking || old.busy != s_st.busy ||
                  (s_st.busy && s_st.total && old.done * 100ull / s_st.total != s_st.done * 100ull / s_st.total);

    /* the library: once the PC has stopped changing things for a moment */
    if (s_st.changes != s_changes_seen) {
        s_changes_seen = s_st.changes;
        s_pending = 1;
        s_last_change = now;
    }
    if (s_st.busy && !old.busy) {
        note("receiving %u bytes: %.60s", s_st.total, s_st.name);
    }
    if (s_pending && !s_st.busy && now - s_last_change > SETTLE_US && !library_scanning()) {
        s_pending = 0;
        note("the PC changed files: rescanning");
        library_rescan();
        ui_toast("Files from the PC" " \xC2\xB7 " "updating the library");
        changed = 1;
    }

    /* The cable is in but the PC never set the device up (seen when sync starts
     * with the cable already in): re-present the device once, then ask for a replug. */
    if ((sceUsbGetState() & PSP_USB_CABLE_CONNECTED) && !s_st.attached) {
        if (!s_cable_since) {
            s_cable_since = now;
        } else if (now - s_cable_since > NUDGE_US) {
            if (!s_nudged) {
                s_nudged = 1;
                sceUsbDeactivate(XLTSYNC_PID);
                sceKernelDelayThread(50000);
                sceUsbActivate(XLTSYNC_PID);
                note("cable in, not attached for 3 s: re-presented the device (state %08x)", sceUsbGetState());
            } else if (!s_hinted) {
                note("still not attached (state %08x): asked for a replug", sceUsbGetState());
                s_hinted = 1;
                ui_toast("USB sync: unplug the cable and plug it in again");
                changed = 1;
            }
            s_cable_since = now;
        }
    } else {
        s_cable_since = 0;
        if (s_st.attached || !(sceUsbGetState() & PSP_USB_CABLE_CONNECTED))
            s_nudged = s_hinted = 0;
    }
    return changed;
}
