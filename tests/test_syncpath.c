/* Host test for plugin/xltsync/syncpath.c: which paths the PC may read and change
 * over USB sync. The PSP's own check, whatever the PC program sends. */
#include <stdio.h>
#include "syncpath.h"

static int fails;

static void want(const char *p, int mode, int expect)
{
    int got = path_ok(p, mode);
    if (got != expect) {
        printf("FAIL path_ok(\"%s\", %s) = %d, want %d\n", p,
               mode == P_READ ? "READ" : mode == P_CHANGE ? "CHANGE" : "MKDIR", got, expect);
        fails++;
    }
}

static void depth(const char *p, int expect)
{
    int got = path_save_depth(p);
    if (got != expect) {
        printf("FAIL path_save_depth(\"%s\") = %d, want %d\n", p, got, expect);
        fails++;
    }
}

static void within(const char *in, const char *out, int expect)
{
    int got = path_within(in, out);
    if (got != expect) {
        printf("FAIL path_within(\"%s\", \"%s\") = %d, want %d\n", in, out, got, expect);
        fails++;
    }
}

int main(void)
{
    /* reading: anywhere on the sticks, and the firmware's fonts */
    want("ms0:/", P_READ, 1);
    want("ms0:/SEPLUGINS/game.txt", P_READ, 1);
    want("ef0:/PSP/SAVEDATA", P_READ, 1);
    want("flash0:/font/jpn0.pgf", P_READ, 1);
    want("flash0:/font/../vsh/x", P_READ, 0);
    want("flash0:/kd/x.prx", P_READ, 0);
    want("host0:/x", P_READ, 0);

    /* XLT's folders: inside them, not themselves */
    want("ms0:/VIDEO/a.mp4", P_CHANGE, 1);
    want("ms0:/video/sub/a.mp4", P_CHANGE, 1);
    want("ms0:/ISO/game.iso", P_CHANGE, 1);
    want("ms0:/PSP/GAME/HB/EBOOT.PBP", P_CHANGE, 1);
    want("ef0:/MUSIC/x.mp3", P_CHANGE, 1);
    want("ms0:/VIDEO", P_CHANGE, 0);
    want("ms0:/VIDEO", P_MKDIR, 1);
    want("ms0:/PSP/GAME", P_CHANGE, 0);
    want("ms0:/PSP/GAME", P_MKDIR, 1);
    want("ms0:/VIDEOS/a.mp4", P_CHANGE, 0);
    want("ms0:/PSP/GAMES/a", P_CHANGE, 0);
    want("ms0:/SEPLUGINS/x.prx", P_CHANGE, 0);
    want("ms0:/PSP/SYSTEM/x", P_CHANGE, 0);
    want("ms0:/PSP/x", P_CHANGE, 0);
    want("ms0:/x.bin", P_CHANGE, 0);

    /* names: the same rules for every folder */
    want("ms0:/VIDEO/../SEPLUGINS/x", P_CHANGE, 0);
    want("ms0:/VIDEO/./a", P_CHANGE, 0);
    want("ms0:/VIDEO//a", P_CHANGE, 0);
    want("ms0:/VIDEO/a/", P_CHANGE, 0);
    want("ms0:/VIDEO/a?b", P_CHANGE, 0);
    want("ms0:/VIDEO/a.", P_CHANGE, 0);
    want("ms0:/VIDEO/a ", P_CHANGE, 0);
    want("ms0:/VIDEO/e\xe9.mp4", P_CHANGE, 0);       /* Latin-1, broken Shift-JIS */
    want("ms0:/VIDEO/\x83\x5c.mp4", P_CHANGE, 1);    /* ソ: trail byte '\' */
    want("ms0:/VIDEO/\xb1.mp4", P_CHANGE, 1);        /* half-width katakana */

    /* save data: a save folder and the files directly in it */
    want("ms0:/PSP/SAVEDATA", P_CHANGE, 0);
    want("ms0:/PSP/SAVEDATA", P_MKDIR, 1);
    want("ms0:/PSP/SAVEDATA/ULUS10041DATA00", P_CHANGE, 1);
    want("ms0:/PSP/SAVEDATA/ULUS10041DATA00", P_MKDIR, 1);
    want("ms0:/PSP/SAVEDATA/ULUS10041DATA00/DATA.BIN", P_CHANGE, 1);
    want("ms0:/psp/savedata/ULUS10041DATA00/PARAM.SFO", P_CHANGE, 1);
    want("ms0:/PSP/SAVEDATA/ULUS10041DATA00/sub/x", P_CHANGE, 0);
    want("ms0:/PSP/SAVEDATA/ULUS10041DATA00/sub/x", P_MKDIR, 0);
    want("ms0:/PSP/SAVEDATA/../SYSTEM/x", P_CHANGE, 0);
    want("ms0:/PSP/SAVEDATAX/a", P_CHANGE, 0);
    want("ef0:/PSP/SAVEDATA/NPJH00001/DATA.BIN", P_CHANGE, 1);
    depth("ms0:/PSP/SAVEDATA", 0);
    depth("ms0:/PSP/SAVEDATA/ULUS10041DATA00", 1);
    depth("ms0:/PSP/SAVEDATA/ULUS10041DATA00/DATA.BIN", 2);
    depth("ms0:/PSP/SAVEDATA/A/B/C", 2);
    depth("ms0:/PSP/GAME/X", 0);
    depth("ms0:/PSP/SAVEDATAX/A", 0);

    /* XLT's own folder is refused for DELETE_TREE if it is inside what goes */
    within("ms0:/PSP/GAME/XLT", "ms0:/PSP/GAME/XLT", 1);
    within("ms0:/PSP/GAME/XLT", "ms0:/psp/game", 1);
    within("ms0:/PSP/GAME/XLT", "ms0:/PSP/GAME/XL", 0);
    within("ms0:/PSP/GAME/XLT", "ms0:/PSP/GAME/XLT2", 0);
    within("ms0:/PSP/GAME/XLT", "ms0:/PSP/GAME/XLT/", 1);
    within("ms0:/PSP/GAME/XLT", "ef0:/PSP/GAME/XLT", 0);

    if (!fails)
        puts("syncpath: all passed");
    return fails != 0;
}
