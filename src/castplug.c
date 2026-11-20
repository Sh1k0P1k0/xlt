/* XLT - installs and switches the XLT Cast plugin (plugin/xltcast). See castplug.h. */
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "castplug.h"
#include "plugins.h"
#include "plugtext.h"
#include "ark.h"
#include "bg.h"
#include "library.h"
#include "xltcast_data.h" /* the plugin, built by plugin/xltcast/Makefile */

#define NAME "xltcast.prx"

/* 1 if the file at path already holds exactly the plugin XLT carries */
static int same_file(const char *path)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    int same = sceIoLseek32(fd, 0, PSP_SEEK_END) == (int)sizeof(xltcast_prx);
    sceIoLseek32(fd, 0, PSP_SEEK_SET);
    static unsigned char buf[4096];
    for (int at = 0; same && at < (int)sizeof(xltcast_prx); at += sizeof(buf)) {
        int n = sizeof(xltcast_prx) - at < sizeof(buf) ? (int)sizeof(xltcast_prx) - at : (int)sizeof(buf);
        same = sceIoRead(fd, buf, n) == n && !memcmp(buf, xltcast_prx + at, n);
    }
    sceIoClose(fd);
    return same;
}

static void job_apply(void *arg)
{
    int on = *(const int *)arg;
    if (!ark_version())
        return;
    const char *dev = "ms0:"; /* the Memory Stick, or a PSP Go's internal storage without one */
    SceUID d = sceIoDopen("ms0:/");
    if (d >= 0) sceIoDclose(d);
    else dev = "ef0:";
    char dir[32], prx[64], note[220];
    snprintf(dir, sizeof(dir), "%s/SEPLUGINS", dev);
    snprintf(prx, sizeof(prx), "%s/" NAME, dir);

    /* the file: when on, written if missing or not this XLT's; when off, only brought
     * up to date if it is there (turned on by hand later, it is the right one) */
    SceIoStat st;
    int present = sceIoGetstat(prx, &st) >= 0;
    if ((on || present) && !same_file(prx)) {
        sceIoMkdir(dir, 0777);
        SceUID fd = sceIoOpen(prx, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
        int ok = fd >= 0 && sceIoWrite(fd, xltcast_prx, sizeof(xltcast_prx)) == (int)sizeof(xltcast_prx);
        if (fd >= 0) sceIoClose(fd);
        if (!ok) {
            sceIoRemove(prx);
            snprintf(note, sizeof(note), "XLT Cast: can't write %s", prx);
            library_note(note);
            return;
        }
        snprintf(note, sizeof(note), "XLT Cast: %s %s", prx, present ? "updated" : "written");
        library_note(note);
    }

    /* the lists: its global lines switched. One that is there but can't be read leaves
     * them all alone (it might hold a line, and writing it would lose the rest). */
    char paths[3][160];
    int n = plugins_file_paths(paths), listed = 0;
    for (int i = 0; i < n; i++) {
        int missing;
        char *t = plugins_read_file(paths[i], &missing);
        if (!t && !missing) {
            snprintf(note, sizeof(note), "XLT Cast: can't read %.140s, lists left alone", paths[i]);
            library_note(note);
            return;
        }
        free(t);
    }
    for (int i = 0; i < n; i++) {
        char *t = plugins_read_text(paths[i]);
        if (!t)
            continue;
        if (plugtext_mentions(t, NAME))
            listed = 1;
        int cap = (int)strlen(t) + 64, changed = 0;
        char *o = malloc(cap);
        int len = o ? plugtext_switch_named(t, NAME, on, o, cap, &changed) : -1;
        if (len >= 0 && changed) {
            int ok = plugins_write_text(paths[i], o, len) == 0;
            snprintf(note, sizeof(note), "XLT Cast: %s in %.140s%s", on ? "on" : "off", paths[i], ok ? "" : " (COULDN'T write)");
            library_note(note);
        }
        free(o);
        free(t);
    }
    if (!on || listed)
        return;

    /* on, and no list has it: a line added */
    char list[64], line[96];
    snprintf(list, sizeof(list), "%s/PLUGINS.TXT", dir);
    snprintf(line, sizeof(line), "game, %s, on", prx);
    int missing;
    char *t = plugins_read_file(list, &missing);
    if (!t && !missing)
        return;
    const char *old = t ? t : "";
    int cap = (int)strlen(old) + (int)strlen(line) + 8;
    char *o = malloc(cap);
    int len = o ? plugtext_append(old, line, o, cap) : -1;
    int ok = len >= 0 && plugins_write_text(list, o, len) == 0;
    free(o);
    free(t);
    snprintf(note, sizeof(note), "XLT Cast: %s %s", ok ? "added to" : "COULDN'T add to", list);
    library_note(note);
}

void castplug_apply(int on)
{
    bg_post(job_apply, &on, sizeof(on));
}
