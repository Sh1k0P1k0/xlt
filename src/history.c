/* XLT - play history. At most HISTORY_MAX entries, oldest dropped first.
 * Play time comes from xltshot, which counts it in games (history_import_play). */
#include <pspiofilemgr.h>
#include <psprtc.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "history.h"
#include "entry.h"
#include "../plugin/xltshot/xltplay.h"

#define HISTORY_MAX 128
#define HISTORY_MAGIC 0x31484C58u /* "XLH1" */

typedef struct {
    char path[sizeof(((Entry *)0)->path)];
    uint64_t played;
    uint32_t count;
    uint32_t seconds; /* time played in all (was padding before play time: 0 then) */
} Record;

static Record s_rec[HISTORY_MAX];
static int s_n;
static char s_file[256];

void history_init(const char *app_dir)
{
    snprintf(s_file, sizeof(s_file), "%.200scache/history.bin", app_dir);
    s_n = 0;
    SceUID fd = sceIoOpen(s_file, PSP_O_RDONLY, 0);
    if (fd < 0)
        return;
    uint32_t h[3];
    if (sceIoRead(fd, h, sizeof(h)) == sizeof(h) && h[0] == HISTORY_MAGIC && h[1] == sizeof(Record) &&
        h[2] <= HISTORY_MAX) {
        int bytes = (int)(h[2] * sizeof(Record));
        if (sceIoRead(fd, s_rec, bytes) == bytes) {
            s_n = (int)h[2];
            for (int i = 0; i < s_n; i++)
                s_rec[i].path[sizeof(s_rec[i].path) - 1] = 0;
        }
    }
    sceIoClose(fd);
}

void history_clear(void)
{
    s_n = 0;
    sceIoRemove(s_file);
}

int history_count(void) { return s_n; }

static Record *find(const char *path)
{
    for (int i = 0; i < s_n; i++)
        if (!strcmp(s_rec[i].path, path))
            return &s_rec[i];
    return NULL;
}

/* as find, ignoring case (FAT does; xltshot may get a path spelled otherwise) */
static Record *find_ci(const char *path)
{
    Record *r = find(path);
    for (int i = 0; !r && i < s_n; i++)
        if (!strcasecmp(s_rec[i].path, path))
            r = &s_rec[i];
    return r;
}

/* a record for path, made (dropping the least recently played one if full) if there is none */
static Record *get(const char *path)
{
    Record *r = find_ci(path);
    if (r)
        return r;
    if (s_n == HISTORY_MAX) {
        int old = 0;
        for (int i = 1; i < s_n; i++)
            if (s_rec[i].played < s_rec[old].played)
                old = i;
        s_rec[old] = s_rec[--s_n];
    }
    r = &s_rec[s_n++];
    memset(r, 0, sizeof(*r));
    snprintf(r->path, sizeof(r->path), "%s", path);
    return r;
}

static void save(void)
{
    SceUID fd = sceIoOpen(s_file, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0)
        return;
    uint32_t h[3] = {HISTORY_MAGIC, sizeof(Record), (uint32_t)s_n};
    sceIoWrite(fd, h, sizeof(h));
    sceIoWrite(fd, s_rec, s_n * sizeof(Record));
    sceIoClose(fd);
}

uint32_t history_seconds(const char *path)
{
    Record *r = find(path);
    return r ? r->seconds : 0;
}

int history_add_play(const char *path, uint64_t started, uint32_t seconds)
{
    if (!path[0] || !seconds)
        return 0;
    Record *r = get(path);
    uint64_t end = started + (uint64_t)seconds * 1000000ull;
    if (!r->count)
        r->count = 1; /* started from the XMB: XLT never saw it launch */
    if (end > r->played)
        r->played = end;
    r->seconds += seconds;
    return 1;
}

int history_import_play(void)
{
    static const char *const devs[] = {"ms0:/", "ef0:/"};
    int added = 0;
    for (int d = 0; d < 2; d++) {
        char p[64];
        snprintf(p, sizeof(p), "%s" XLTPLAY_FILE, devs[d]);
        SceUID fd = sceIoOpen(p, PSP_O_RDONLY, 0);
        if (fd < 0)
            continue;
        XltPlayRecord rec;
        int ok = 1;
        for (;;) {
            int n = sceIoRead(fd, &rec, sizeof(rec));
            if (n == 0)
                break;
            if (n != (int)sizeof(rec)) {
                ok = n > 0; /* a record cut off at the end: the rest was read */
                break;
            }
            if (rec.magic != XLTPLAY_MAGIC)
                continue;
            rec.path[sizeof(rec.path) - 1] = 0;
            if (!rec.path[0]) { /* the PSP didn't say what ran: the game XLT launched just before */
                Record *best = NULL;
                for (int i = 0; i < s_n; i++)
                    if (s_rec[i].played <= rec.started &&
                        rec.started - s_rec[i].played <= XLTPLAY_MATCH_S * 1000000ull &&
                        (!best || s_rec[i].played > best->played))
                        best = &s_rec[i];
                if (!best)
                    continue; /* started from the XMB: can't tell which game it was */
                snprintf(rec.path, sizeof(rec.path), "%s", best->path);
            }
            added += history_add_play(rec.path, rec.started, rec.seconds);
        }
        sceIoClose(fd);
        if (added)
            save();
        if (ok)
            sceIoRemove(p); /* counted: never twice */
    }
    return added;
}

uint64_t history_played(const char *path)
{
    Record *r = find(path);
    return r ? r->played : 0;
}

void history_record(const char *path)
{
    u64 now;
    sceRtcGetCurrentTick(&now);
    Record *r = get(path);
    r->played = now;
    r->count++;
    save();
}

/* "12 h 40 min", "45 min", "under a minute"; "" if none */
void history_time_text(uint32_t s, char *out, int n)
{
    if (!s) out[0] = 0;
    else if (s < 60) snprintf(out, n, "under a minute");
    else if (s < 3600) snprintf(out, n, "%u min", (unsigned)(s / 60));
    else if (s < 100 * 3600) snprintf(out, n, "%u h %u min", (unsigned)(s / 3600), (unsigned)(s % 3600 / 60));
    else snprintf(out, n, "%u h", (unsigned)(s / 3600));
}

void history_describe(const char *path, char *out, int n)
{
    uint64_t t = history_played(path);
    if (!t) {
        snprintf(out, n, "Never played");
        return;
    }
    u64 now;
    sceRtcGetCurrentTick(&now);
    uint32_t days = now > t ? (uint32_t)((now - t) / (86400ull * 1000000ull)) : 0;
    char when[32], total[24];
    if (days == 0) snprintf(when, sizeof(when), "played today");
    else if (days == 1) snprintf(when, sizeof(when), "played yesterday");
    else if (days < 14) snprintf(when, sizeof(when), "played %u days ago", (unsigned)days);
    else if (days < 60) snprintf(when, sizeof(when), "played %u weeks ago", (unsigned)(days / 7));
    else snprintf(when, sizeof(when), "played %u months ago", (unsigned)(days / 30));
    history_time_text(history_seconds(path), total, sizeof(total));
    if (total[0]) {
        snprintf(out, n, "%s \xC2\xB7 %s", total, when);
    } else {
        when[0] = 'P';
        snprintf(out, n, "%s", when);
    }
}
