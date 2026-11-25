/* XLT - ARK SETTINGS.TXT editing.
 *
 * SETTINGS.TXT lines are "runlevel, key, on|off". The runlevel can be a game
 * ID, which makes the line apply to that game only. Clock keys:
 *   ARK-5: cpuclock:133 | cpuclock:222 | cpuclock:333
 *   ARK-4: powersave | defaultclock | overclock
 * XLT only ever touches clock lines for one runlevel at a time and keeps
 * every other line exactly as it was (same approach as psp5's arkcfg.c, which
 * also leaves game-ID lines alone).
 */
/* Parts of this file (finding SETTINGS.TXT) are adapted from psp5 by 08Weber,
 * https://github.com/08Weber/psp5, GPL-3.0. */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ark.h"
#include "launch.h"
#include "arktext.h"
#include "arkcfg.h"

#define MAX_FILE (64 * 1024)
#define FLASH_SETTINGS "flash1:/SETTINGS.TXT"

static int s_version;
static char s_dir[128];

static int dir_exists(const char *path)
{
    SceUID d = sceIoDopen(path);
    if (d < 0)
        return 0;
    sceIoDclose(d);
    return 1;
}

static int file_exists(const char *path)
{
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0;
}

void ark_init(void)
{
    s_version = 0;
    s_dir[0] = 0;
    if (!launch_supported())
        return;
    s_version = sctrlSEGetVersion() == 4 ? 4 : 5;
    static const char *cand[] = {"ms0:/PSP/SAVEDATA/ARK_01234/", "ef0:/PSP/SAVEDATA/ARK_01234/"};
    for (int i = 0; i < 2; i++)
        if (dir_exists(cand[i])) {
            snprintf(s_dir, sizeof(s_dir), "%s", cand[i]);
            return;
        }
    if (s_version == 4 && file_exists("ms0:/SEPLUGINS/SETTINGS.TXT"))
        snprintf(s_dir, sizeof(s_dir), "ms0:/SEPLUGINS/"); /* ARK-4 without an ARK folder */
}

int ark_version(void) { return s_version; }
const char *ark_dir(void) { return s_dir; }

const char *clock_label(int c)
{
    static const char *n[CLOCK_COUNT] = {"Auto", "133 MHz", "222 MHz", "333 MHz"};
    return c >= 0 && c < CLOCK_COUNT ? n[c] : "Auto";
}

/* ------------------------------------------------------------- file text */

static char *read_text(const char *path)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return NULL;
    int size = sceIoLseek32(fd, 0, PSP_SEEK_END);
    sceIoLseek32(fd, 0, PSP_SEEK_SET);
    char *buf = NULL;
    if (size >= 0 && size <= MAX_FILE && (buf = malloc(size + 1))) {
        int n = sceIoRead(fd, buf, size);
        buf[n > 0 ? n : 0] = 0;
    }
    sceIoClose(fd);
    return buf;
}

static int write_text(const char *path, const char *text, int len)
{
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0)
        return -1;
    int ok = sceIoWrite(fd, text, len) == len;
    sceIoClose(fd);
    return ok ? 0 : -1;
}

static void settings_path(char *out, int n)
{
    snprintf(out, n, "%sSETTINGS.TXT", s_dir);
}

static char *load_settings(void);

static char s_self[128]; /* XLT's own EBOOT, for its clock line */

void ark_set_self(const char *path) { snprintf(s_self, sizeof(s_self), "%s", path); }

/* Keeps XLT's clock its own in text about to be written (*out, *len, room for cap):
 * a Settings change that adds "game, cpuclock:..." puts it after XLT's line. */
static void keep_free(char **out, int *len, int cap)
{
    if (!s_self[0] || *len < 0)
        return;
    char *fixed = malloc(cap + 256);
    if (!fixed)
        return;
    (*out)[*len] = 0;
    int n = arktext_free_clock(*out, s_self, s_version == 4, fixed, cap + 256);
    if (n > 0) {
        free(*out);
        *out = fixed;
        *len = n;
    } else {
        free(fixed);
    }
}

int ark_free_clock(void)
{
    if (!s_version || !s_dir[0] || !s_self[0])
        return 0;
    char *text = load_settings();
    if (!text)
        return 0; /* no file: ARK's defaults set no clock */
    int cap = (int)strlen(text) + 512;
    char *out = malloc(cap);
    int n = out ? arktext_free_clock(text, s_self, s_version == 4, out, cap) : -1;
    int r = n;
    if (n > 0) {
        char path[160];
        settings_path(path, sizeof(path));
        r = write_text(path, out, n) == 0 ? 1 : -1;
        if (r == 1 && s_version == 5 && file_exists(FLASH_SETTINGS))
            write_text(FLASH_SETTINGS, out, n);
    }
    free(out);
    free(text);
    return r;
}


static char *load_settings(void)
{
    char path[160];
    settings_path(path, sizeof(path));
    char *t = read_text(path);
    if (!t && s_version == 5)
        t = read_text(FLASH_SETTINGS); /* ARK-5 installed to flash */
    return t;
}

int ark_get_clock(const char *id)
{
    if (!s_version)
        return CLOCK_AUTO;
    char *text = load_settings();
    if (!text)
        return CLOCK_AUTO;
    int c = arktext_get_clock(text, id);
    free(text);
    return c;
}

int ark_set_clock(const char *id, int clock)
{
    if (!s_version || !s_dir[0])
        return -1;
    char *text = load_settings();
    const int cap = MAX_FILE + 512;
    char *out = malloc(cap);
    if (!out) {
        free(text);
        return -1;
    }
    int len = arktext_set_clock(text ? text : "", id, clock, s_version == 4, out, cap);
    keep_free(&out, &len, cap);
    char path[160];
    settings_path(path, sizeof(path));
    int r = len < 0 ? -1 : write_text(path, out, len);
    /* ARK-5 installed to flash reads flash1:/SETTINGS.TXT: keep that copy in step. */
    if (len >= 0 && s_version == 5 && file_exists(FLASH_SETTINGS)) {
        int f = write_text(FLASH_SETTINGS, out, len);
        if (r)
            r = f;
    }
    free(out);
    free(text);
    return r;
}

/* ---------------------------------------------------- whole settings file */

int ark_load_cfg(ArkCfg *c, char *extra, int cap)
{
    int ark4 = s_version == 4;
    char *text = s_version ? load_settings() : NULL;
    if (!text) {
        arkcfg_defaults(c, ark4); /* no file yet: what ARK uses */
        if (cap > 0) extra[0] = 0;
        return s_version && s_dir[0] ? 0 : -1;
    }
    arkcfg_parse(c, text, ark4, extra, cap);
    free(text);
    return 0;
}

int ark_save_cfg(const ArkCfg *c, const char *extra)
{
    if (!s_version || !s_dir[0])
        return -1;
    const int cap = MAX_FILE + 2048;
    char *out = malloc(cap);
    if (!out)
        return -1;
    int len = arkcfg_write(c, s_version == 4, extra, out, cap);
    keep_free(&out, &len, cap);
    char path[160];
    settings_path(path, sizeof(path));
    int r = len < 0 ? -1 : write_text(path, out, len);
    if (len >= 0 && s_version == 5 && file_exists(FLASH_SETTINGS)) {
        int f = write_text(FLASH_SETTINGS, out, len);
        if (r)
            r = f;
    }
    free(out);
    return r;
}
