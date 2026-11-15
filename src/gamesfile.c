/* XLT - cache/games.txt for XLT Tool. See gamesfile.h. */
#include <stdio.h>
#include <string.h>
#include "gamesfile.h"
#ifndef GAMESFILE_PURE
#include <pspiofilemgr.h>
#include "gamecfg.h"
#include "history.h"
#endif

static const char *kind_name(int k)
{
    switch (k) {
    case KIND_ISO: return "iso";
    case KIND_CSO: return "cso";
    case KIND_ZSO: return "zso";
    case KIND_PSN: return "psn";
    case KIND_PS1: return "ps1";
    case KIND_HOMEBREW: return "homebrew";
    default: return NULL;
    }
}

/* src into out as a field: tabs and line breaks become spaces */
static int field(char *out, int n, const char *src, int max)
{
    int o = 0;
    for (int i = 0; i < max && src[i] && o < n - 1; i++) {
        char c = src[i];
        out[o++] = (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    }
    out[o] = 0;
    return o;
}

#define UNIX_EPOCH_S 62135596800ull /* seconds from year 1 to 1970, as the RTC counts */

int gamesfile_line(const Entry *e, uint64_t played, uint32_t seconds, int favorite, char *out, int n)
{
    const char *k = kind_name(e->kind);
    if (!k || n < 64)
        return 0;
    char path[sizeof(e->path)], id[sizeof(e->id)], title[sizeof(e->title)];
    field(path, sizeof(path), e->path, sizeof(e->path));
    field(id, sizeof(id), e->id, sizeof(e->id));
    field(title, sizeof(title), e->title, sizeof(e->title));
    uint64_t unix_s = played / 1000000ull > UNIX_EPOCH_S ? played / 1000000ull - UNIX_EPOCH_S : 0;
    int r = snprintf(out, n, "%s\t%s\t%s\t%llu\t%u\t%d\t%s\n", k, path, id, (unsigned long long)unix_s,
                     (unsigned)seconds, favorite ? 1 : 0, title);
    return r > 0 && r < n ? r : 0;
}

#ifndef GAMESFILE_PURE
void gamesfile_write(const char *cache_dir, const Entry *e, int n)
{
    char p[256], tmp[256];
    snprintf(p, sizeof(p), "%.200s/games.txt", cache_dir);
    snprintf(tmp, sizeof(tmp), "%.200s/games.txt.part", cache_dir);
    SceUID fd = sceIoOpen(tmp, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0)
        return;
    int ok = sceIoWrite(fd, GAMESFILE_HEAD, strlen(GAMESFILE_HEAD)) == (int)strlen(GAMESFILE_HEAD);
    char line[512];
    for (int i = 0; ok && i < n; i++) {
        int len = gamesfile_line(&e[i], history_played(e[i].path), history_seconds(e[i].path),
                                 favorite_get(e[i].path), line, sizeof(line));
        if (len && sceIoWrite(fd, line, len) != len)
            ok = 0;
    }
    sceIoClose(fd);
    if (ok) {
        sceIoRemove(p);
        sceIoRename(tmp, p);
    } else {
        sceIoRemove(tmp);
    }
}
#endif
