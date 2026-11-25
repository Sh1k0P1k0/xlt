/* XLT - Internet over USB with USBNet (by chriopter, github.com/chriopter/pspkit-usbnet). See netplug.h. */
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "netplug.h"
#include "side.h"
#include "plugins.h"
#include "plugtext.h"
#include "ark.h"
#include "bg.h"
#include "library.h"

#define PRX "usbnet.prx"
#define OURS_NAME "xltnet.prx" /* XLT's own plugin in development builds, replaced by USBNet */

static int exists(const char *path)
{
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0 && !FIO_S_ISDIR(st.st_mode);
}

/* from -> to, through to + ".part"; 0 or -1 */
static int copy_file(const char *from, const char *to)
{
    char part[200];
    snprintf(part, sizeof(part), "%s.part", to);
    SceUID in = sceIoOpen(from, PSP_O_RDONLY, 0);
    if (in < 0)
        return -1;
    SceUID out = sceIoOpen(part, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    int ok = out >= 0;
    static unsigned char buf[8192];
    int n;
    while (ok && (n = sceIoRead(in, buf, sizeof(buf))) > 0)
        ok = sceIoWrite(out, buf, n) == n;
    sceIoClose(in);
    if (out >= 0)
        sceIoClose(out);
    if (ok)
        ok = sceIoRename(part, to) >= 0;
    if (!ok)
        sceIoRemove(part);
    return ok ? 0 : -1;
}

/* a file name XLT Tool may leave: letters, digits, . _ - only, not starting with a dot */
static int plain_name(const char *n)
{
    if (!n[0] || n[0] == '.' || strlen(n) > 40)
        return 0;
    for (; *n; n++)
        if (!((*n >= 'a' && *n <= 'z') || (*n >= 'A' && *n <= 'Z') || (*n >= '0' && *n <= '9') || strchr("._-", *n)))
            return 0;
    return 1;
}

/* USBNet's release as XLT Tool left it in staged/ goes into folder/ (seplugins/usbnet/, as PSPDX
 * lays it out): every file, then staged/ removed. 0, or -1 with folder/ left as far as it got. */
static int install_staged(const char *staged, const char *folder)
{
    SceUID d = sceIoDopen(staged);
    if (d < 0)
        return -1;
    char names[8][48];
    int n = 0;
    SceIoDirent e;
    memset(&e, 0, sizeof(e));
    while (sceIoDread(d, &e) > 0) {
        if (!FIO_S_ISDIR(e.d_stat.st_mode) && plain_name(e.d_name) && n < 8)
            snprintf(names[n++], sizeof(names[0]), "%s", e.d_name);
        memset(&e, 0, sizeof(e));
    }
    sceIoDclose(d);
    sceIoMkdir(folder, 0777);
    char from[160], to[160];
    for (int i = 0; i < n; i++) {
        snprintf(from, sizeof(from), "%.100s/%.47s", staged, names[i]);
        snprintf(to, sizeof(to), "%.100s/%.47s", folder, names[i]);
        if (copy_file(from, to) < 0)
            return -1;
    }
    for (int i = 0; i < n; i++) {
        snprintf(from, sizeof(from), "%.100s/%.47s", staged, names[i]);
        sceIoRemove(from);
    }
    sceIoRmdir(staged);
    return 0;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* 1 if a PLUGINS.TXT path names the file at want ("ms0:/seplugins/..."): without case, and a
 * path without a device is ms0:'s */
static int same_path(const char *path, const char *want)
{
    if (path[0] == '/' && !strncmp(want, "ms0:", 4))
        want += 4;
    if (strlen(path) != strlen(want))
        return 0;
    for (; *path; path++, want++)
        if (lower((unsigned char)*path) != lower((unsigned char)*want))
            return 0;
    return 1;
}

/* The last line of any list that names `path` switched on or off, in place where it can be (PSPDX's
 * own way); xltnet.prx's lines off. *found = a line names it. -1 when a list can't be read. */
static int set_line(const char *path, int on, int *found)
{
    char paths[3][160], note[220];
    int n = plugins_file_paths(paths), last_file = -1, last_line = -1;
    *found = 0;
    static PlugLine lines[PLUG_MAX];
    for (int i = 0; i < n; i++) {
        int missing;
        char *t = plugins_read_file(paths[i], &missing);
        if (!t && !missing) {
            snprintf(note, sizeof(note), "Internet over USB: can't read %.140s, lists left alone", paths[i]);
            library_note(note);
            return -1;
        }
        if (!t)
            continue;
        int count = plugtext_lines(t, i, lines, PLUG_MAX);
        for (int k = 0; k < count; k++)
            if (same_path(lines[k].path, path))
                last_file = i, last_line = lines[k].line;
        /* XLT's former plugin: off */
        int cap = (int)strlen(t) + 64, changed = 0;
        char *o = malloc(cap);
        int len = o ? plugtext_switch_named(t, OURS_NAME, 0, o, cap, &changed) : -1;
        if (len >= 0 && changed)
            plugins_write_text(paths[i], o, len);
        free(o);
        free(t);
    }
    if (last_file < 0)
        return 0;
    *found = 1;
    char *t = plugins_read_text(paths[last_file]);
    if (!t)
        return -1;
    int r = plugtext_switch_in_place(t, last_line, on), ok = 1;
    if (r == 1) {
        ok = plugins_write_text(paths[last_file], t, strlen(t)) == 0;
    } else if (r < 0) { /* "on" at the line's end, "1", ...: the line written again */
        int cap = (int)strlen(t) + 64;
        char *o = malloc(cap);
        int len = o ? plugtext_set_line(t, last_line, NULL, on, o, cap) : -1;
        ok = len >= 0 && plugins_write_text(paths[last_file], o, len) == 0;
        free(o);
    }
    if (r != 0) {
        snprintf(note, sizeof(note), "Internet over USB: USBNet %s in %.120s%s", on ? "on" : "off", paths[last_file],
                 ok ? "" : " (COULDN'T write)");
        library_note(note);
    }
    free(t);
    return 0;
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
    char folder[64], in_folder[80], flat[64], staged[80], note[220];
    snprintf(folder, sizeof(folder), "%s/seplugins/usbnet", dev);
    snprintf(in_folder, sizeof(in_folder), "%s/" PRX, folder);
    snprintf(flat, sizeof(flat), "%s/seplugins/" PRX, dev);
    snprintf(staged, sizeof(staged), "%s/PSP/GAME/XLT/USBNET", dev);

    /* installed already (PSPDX's folder, or the one file the README says): left as it is */
    const char *have = exists(in_folder) ? in_folder : exists(flat) ? flat : NULL;
    if (!have && on) {
        char staged_prx[100];
        snprintf(staged_prx, sizeof(staged_prx), "%s/" PRX, staged);
        if (exists(staged_prx)) {
            int ok = install_staged(staged, folder) == 0 && exists(in_folder);
            snprintf(note, sizeof(note), "Internet over USB: USBNet %s %s", ok ? "installed in" : "COULDN'T be installed in",
                     folder);
            library_note(note);
            if (ok)
                have = in_folder;
        }
    }
    int found;
    if (!have) {
        set_line(in_folder, 0, &found); /* (xltnet's lines off all the same) */
        if (on)
            library_note("Internet over USB: USBNet isn't on the stick yet: turn on Internet in XLT Tool "
                         "with USB sync on, or install it with PSPDX");
        return;
    }
    if (set_line(have, on, &found) < 0 || found || !on)
        return;

    /* on, and no list names it: PSPDX's line, after the end of seplugins/PLUGINS.TXT */
    char list[64], line[120];
    snprintf(list, sizeof(list), "%s/SEPLUGINS/PLUGINS.TXT", dev);
    int missing;
    char *t = plugins_read_file(list, &missing);
    if (!t && !missing)
        return;
    const char *old = t ? t : "";
    snprintf(line, sizeof(line), "always, %s, on ", have);
    int cap = (int)strlen(old) + (int)strlen(line) + 8;
    char *o = malloc(cap);
    int len = o ? plugtext_append(old, line, o, cap) : -1;
    int ok = len >= 0 && plugins_write_text(list, o, len) == 0;
    free(o);
    free(t);
    snprintf(note, sizeof(note), "Internet over USB: USBNet %s %s", ok ? "added to" : "COULDN'T be added to", list);
    library_note(note);
}

void netplug_apply(int on)
{
    if (XLT_SIDE_BUILD) return; /* nuMB: the plugins are the real XLT's (side.h) */
    bg_post(job_apply, &on, sizeof(on));
}
