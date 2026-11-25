/* XLTCRASH test: crashes on purpose, to check the crash log (plugin/xltcrash).
 * make, then copy EBOOT.PBP to PSP/GAME/XLTCRASH/ and start it from XLT or the XMB,
 * with Settings > XLT > Crash log on (and XLT restarted once, so the plugin is in place).
 *
 * The crashes run in a thread of their own ("crash test thread"), three calls deep, so
 * this app stays alive: a second later it shows the end of SEPLUGINS/xltcrash.log.
 * SELECT crashes the app's main thread instead (then only HOME gets out). */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <string.h>
#include <stdio.h>

PSP_MODULE_INFO("XLTCrashTest", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define printf pspDebugScreenPrintf

static volatile int s_kind;

static void __attribute__((noinline)) crash_now(int kind)
{
    switch (kind) {
    case 0: *(volatile int *)0 = 1; break;                    /* store to address 0 */
    case 1: (void)*(volatile int *)0x00000004; break;          /* load from address 4 */
    case 2: ((void (*)(void))0x00000010)(); break;             /* jump to nothing */
    case 3: asm volatile("break"); break;                      /* break instruction */
    }
}
static void __attribute__((noinline)) level3(int k) { crash_now(k); asm volatile(""); }
static void __attribute__((noinline)) level2(int k) { level3(k); asm volatile(""); }
static void __attribute__((noinline)) level1(int k) { level2(k); asm volatile(""); }

static int crash_thread(SceSize args, void *argp)
{
    level1(s_kind);
    return 0;
}

static int exit_cb(int a, int b, void *c) { sceKernelExitGame(); return 0; }
static int cb_thread(SceSize args, void *argp)
{
    int cb = sceKernelCreateCallback("Exit Callback", exit_cb, NULL);
    sceKernelRegisterExitCallback(cb);
    sceKernelSleepThreadCB();
    return 0;
}

/* the last lines of the log, on screen */
static void show_log(void)
{
    static char buf[2400];
    const char *paths[] = {"ms0:/SEPLUGINS/xltcrash.log", "ef0:/SEPLUGINS/xltcrash.log"};
    for (int p = 0; p < 2; p++) {
        SceUID fd = sceIoOpen(paths[p], PSP_O_RDONLY, 0);
        if (fd < 0) continue;
        int size = sceIoLseek32(fd, 0, PSP_SEEK_END);
        int from = size > (int)sizeof(buf) - 1 ? size - (int)sizeof(buf) + 1 : 0;
        sceIoLseek32(fd, from, PSP_SEEK_SET);
        int n = sceIoRead(fd, buf, sizeof(buf) - 1);
        sceIoClose(fd);
        buf[n > 0 ? n : 0] = 0;
        char *last = strstr(buf, "=== crash");
        for (char *q = last; q && (q = strstr(q + 1, "=== crash")); ) last = q;
        printf("\n%s (%d bytes), last entry:\n\n%s", paths[p], size, last ? last : buf);
        return;
    }
    printf("\nNo SEPLUGINS/xltcrash.log: the crash wasn't logged.\n"
           "Is Settings > XLT > Crash log on, and was XLT restarted since?\n");
}

static void menu(void)
{
    pspDebugScreenClear();
    printf("XLT crash log test\n\n"
           "In a thread of its own (the app stays alive, the log is shown):\n"
           "  X        store to address 0\n"
           "  SQUARE   load from address 4\n"
           "  TRIANGLE jump to address 0x10\n"
           "  CIRCLE   break instruction\n"
           "  SELECT   store to address 0 in the main thread (HOME to leave)\n"
           "  START    quit\n");
}

int main(void)
{
    pspDebugScreenInit();
    SceUID cbt = sceKernelCreateThread("update_thread", cb_thread, 0x11, 0xFA0, 0, 0);
    if (cbt >= 0) sceKernelStartThread(cbt, 0, 0);
    sceCtrlSetSamplingCycle(0);
    menu();
    unsigned old = 0;
    for (;;) {
        SceCtrlData pad;
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned now = pad.Buttons & ~old;
        old = pad.Buttons;
        int kind = now & PSP_CTRL_CROSS ? 0 : now & PSP_CTRL_SQUARE ? 1 : now & PSP_CTRL_TRIANGLE ? 2
                 : now & PSP_CTRL_CIRCLE ? 3 : -1;
        if (now & PSP_CTRL_START)
            break;
        if (now & PSP_CTRL_SELECT) {
            pspDebugScreenClear();
            printf("Crashing the main thread now. HOME to leave, then look at SEPLUGINS/xltcrash.log.\n");
            sceKernelDelayThread(200000);
            level1(0);
        }
        if (kind >= 0) {
            menu();
            s_kind = kind;
            SceUID t = sceKernelCreateThread("crash test thread", crash_thread, 0x20, 0x4000, PSP_THREAD_ATTR_USER, 0);
            int r = t >= 0 ? sceKernelStartThread(t, 0, 0) : t;
            printf("\ncrash %d started in thread %08x (%08x), waiting 1.5 s...\n", kind, t, r);
            sceKernelDelayThread(1500000);
            show_log();
        }
        sceDisplayWaitVblankStart();
    }
    sceKernelExitGame();
    return 0;
}
