/* XLT - the update check and install. See update.h. */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mbedtls/sha256.h>
#include "update.h"
#include "arklauncher.h"
#include "bg.h"
#include "version.h"
#include "webfetch.h"
#include "zip.h"

#define API_URL "https://api.github.com/repos/boubounokefalos/xlt/releases/latest"
#define JSON_CAP (256 * 1024)
#define CHUNK (64 * 1024)
#define MIN_EBOOT (256 * 1024) /* anything smaller is not XLT */

static char s_dir[256];
static char s_json[300], s_zip[300], s_new[300], s_self[300], s_old[300];
static char s_backup[64];
static volatile int s_state;
static int s_known;
static UpdRelease s_rel;
static char s_notes[UPD_NOTES_MAX];
static char s_err[160];
static volatile const char *s_act = "";
static int s_restart;

void update_init(const char *dir)
{
    snprintf(s_dir, sizeof(s_dir), "%s", dir);
    snprintf(s_json, sizeof(s_json), "%scache/update.json", dir);
    snprintf(s_zip, sizeof(s_zip), "%scache/update.zip", dir);
    snprintf(s_new, sizeof(s_new), "%sEBOOT.NEW", dir);
    snprintf(s_self, sizeof(s_self), "%sEBOOT.PBP", dir);
    snprintf(s_backup, sizeof(s_backup), "EBOOT-%s.PBP", XLT_VERSION);
    snprintf(s_old, sizeof(s_old), "%s%s", dir, s_backup);
    /* what a stopped or failed attempt left */
    sceIoRemove(s_zip);
    sceIoRemove(s_new);
    sceIoRemove(s_json);
}

int update_state(void) { return s_state; }
int update_busy(void) { return s_state == UP_CHECKING || s_state == UP_DOWNLOADING || s_state == UP_INSTALLING; }
const UpdRelease *update_release(void) { return &s_rel; }
const char *update_notes(void) { return s_notes; }
const char *update_error(void) { return s_err; }
const char *update_backup(void) { return s_backup; }
int update_known(void) { return s_known; }
void update_request_restart(void) { s_restart = 1; }

int update_take_restart(void)
{
    int r = s_restart;
    s_restart = 0;
    return r;
}

static void fail(const char *fmt, const char *arg)
{
    snprintf(s_err, sizeof(s_err), fmt, arg ? arg : "");
    wf_note("update: failed: %s", s_err);
    s_state = UP_FAILED;
}

const char *update_activity(void)
{
    if (s_state == UP_INSTALLING)
        return (const char *)s_act;
    int phase;
    int64_t g, t;
    wf_progress(&phase, &g, &t);
    if (phase == WF_WIFI) return "Connecting to Wi-Fi...";
    if (s_state == UP_CHECKING) return "Asking GitHub for the latest release...";
    return "From GitHub, over Wi-Fi";
}

void update_progress(int64_t *got, int64_t *total, int *kbps)
{
    if (!wf_download_progress(got, total, kbps) && s_state != UP_DOWNLOADING) {
        *got = *total = 0;
        *kbps = 0;
    }
    if (*total <= 0 && s_rel.zip_size > 0) *total = s_rel.zip_size;
}

void update_check(void)
{
    if (update_busy() || s_state == UP_INSTALLED) return;
    s_err[0] = 0;
    if (wf_fetch_file(API_URL, s_json) < 0) {
        fail("Another download is still running: try again in a moment", NULL);
        return;
    }
    wf_note("update: checking (XLT %s)", XLT_VERSION);
    s_state = UP_CHECKING;
}

void update_install(void)
{
    if (s_state != UP_AVAILABLE) return;
    s_err[0] = 0;
    if (wf_fetch_file(s_rel.zip_url, s_zip) < 0) {
        fail("Another download is still running: try again in a moment", NULL);
        return;
    }
    wf_note("update: downloading %s (%lld bytes)", s_rel.zip_url, (long long)s_rel.zip_size);
    s_state = UP_DOWNLOADING;
}

void update_stop(void)
{
    if (s_state == UP_CHECKING || s_state == UP_DOWNLOADING) wf_download_stop();
}

void update_dismiss(void)
{
    if (s_state == UP_INSTALLING) return;
    update_stop();
    if (s_state != UP_INSTALLED) s_state = UP_IDLE;
    wf_disconnect();
}

/* ------------------------------------------------------------ the check */

static void read_answer(void)
{
    char *buf = malloc(JSON_CAP + 1);
    SceUID fd = sceIoOpen(s_json, PSP_O_RDONLY, 0);
    int len = buf && fd >= 0 ? sceIoRead(fd, buf, JSON_CAP) : -1;
    if (fd >= 0) sceIoClose(fd);
    sceIoRemove(s_json);
    if (len <= 0) {
        free(buf);
        fail("Couldn't read GitHub's answer", NULL);
        return;
    }
    buf[len] = 0;
    int r = upd_parse_release(buf, len, &s_rel);
    if (r < 0) {
        wf_note("update: not a release: %.200s", buf);
        int limited = strstr(buf, "rate limit") != NULL;
        free(buf);
        memset(&s_rel, 0, sizeof(s_rel));
        fail(limited ? "GitHub is limiting checks from this network: try again in an hour"
                     : "GitHub didn't list a release", NULL);
        return;
    }
    free(buf);
    upd_plain_notes(s_rel.notes, s_notes, sizeof(s_notes));
    int newer = upd_version_cmp(s_rel.version, XLT_VERSION) > 0 && !s_rel.draft && !s_rel.prerelease;
    wf_note("update: latest is %s (%s, %lld bytes, sha256 %s): %s", s_rel.version, s_rel.zip_name,
            (long long)s_rel.zip_size, s_rel.sha256[0] ? s_rel.sha256 : "not given", newer ? "newer" : "not newer");
    s_known = newer ? 2 : 1;
    s_state = newer ? UP_AVAILABLE : UP_LATEST;
}

/* ---------------------------------------------------------- the install */

static uint8_t *s_buf; /* CHUNK * 2 while installing */

static int zread(void *ctx, uint64_t off, void *dst, uint32_t len)
{
    SceUID fd = *(SceUID *)ctx;
    if (sceIoLseek(fd, (SceOff)off, PSP_SEEK_SET) != (SceOff)off) return -1;
    return sceIoRead(fd, dst, len);
}

static int zwrite(void *ctx, const void *src, uint32_t len)
{
    return sceIoWrite(*(SceUID *)ctx, src, len) == (int)len ? (int)len : -1;
}

static int file_size(const char *p, int64_t *size)
{
    SceIoStat st;
    if (sceIoGetstat(p, &st) < 0) return -1;
    *size = st.st_size;
    return 0;
}

static int sha256_file(const char *path, char hex[65])
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return -1;
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c);
    mbedtls_sha256_starts_ret(&c, 0);
    int n, ok = 1;
    while ((n = sceIoRead(fd, s_buf, CHUNK)) > 0)
        mbedtls_sha256_update_ret(&c, s_buf, n);
    if (n < 0) ok = 0;
    sceIoClose(fd);
    unsigned char d[32];
    mbedtls_sha256_finish_ret(&c, d);
    mbedtls_sha256_free(&c);
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", d[i]);
    return ok ? 0 : -1;
}

/* EBOOT.NEW from the zip: 0, or -1 with s_err set. */
static int unpack(void)
{
    SceUID in = sceIoOpen(s_zip, PSP_O_RDONLY, 0);
    if (in < 0) { fail("Couldn't open the download", NULL); return -1; }
    ZipDir z;
    int r = zip_open(&z, zread, &in, (uint64_t)sceIoLseek(in, 0, PSP_SEEK_END));
    if (r != ZIP_OK) {
        sceIoClose(in);
        fail("The download isn't a usable zip: %s", zip_error_text(r));
        return -1;
    }
    ZipEntry e, best;
    int rank = 0;
    while (zip_next(&z, &e) == 1) {
        int k = e.is_dir ? 0 : upd_eboot_rank(e.name);
        if (k > rank) { rank = k; best = e; }
    }
    if (!rank) {
        zip_close(&z);
        sceIoClose(in);
        fail("There's no EBOOT.PBP in the download", NULL);
        return -1;
    }
    wf_note("update: unpacking %s (%u bytes)", best.name, (unsigned)best.usize);
    SceUID out = sceIoOpen(s_new, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (out < 0) {
        zip_close(&z);
        sceIoClose(in);
        fail("Couldn't write in XLT's folder", NULL);
        return -1;
    }
    r = zip_extract(&z, &best, zwrite, &out, s_buf, CHUNK, s_buf + CHUNK, CHUNK, NULL);
    sceIoClose(out);
    zip_close(&z);
    sceIoClose(in);
    if (r != ZIP_OK) {
        sceIoRemove(s_new);
        fail(r == ZIP_ERR_WRITE ? "Couldn't write the new XLT: is the Memory Stick full?" : "Couldn't unpack it: %s",
             zip_error_text(r));
        return -1;
    }
    /* a PBP of the size the zip says */
    int64_t size = 0;
    char magic[4] = {0};
    SceUID fd = sceIoOpen(s_new, PSP_O_RDONLY, 0);
    if (fd >= 0) {
        sceIoRead(fd, magic, 4);
        sceIoClose(fd);
    }
    if (file_size(s_new, &size) < 0 || size != (int64_t)best.usize || size < MIN_EBOOT || memcmp(magic, "\0PBP", 4)) {
        sceIoRemove(s_new);
        fail("What was unpacked isn't an EBOOT.PBP", NULL);
        return -1;
    }
    return 0;
}

static void job_install(void *arg)
{
    (void)arg;
    s_buf = malloc(CHUNK * 2);
    if (!s_buf) { fail("Not enough memory to install", NULL); return; }
    int64_t size = 0;
    if (file_size(s_zip, &size) < 0 || (s_rel.zip_size > 0 && size != s_rel.zip_size)) {
        fail("The download is incomplete", NULL);
        goto out;
    }
    if (s_rel.sha256[0]) {
        s_act = "Checking the download...";
        char hex[65];
        if (sha256_file(s_zip, hex) < 0) { fail("Couldn't read the download", NULL); goto out; }
        if (strcmp(hex, s_rel.sha256)) {
            wf_note("update: sha256 %s, GitHub says %s", hex, s_rel.sha256);
            fail("The download is damaged (its checksum is wrong)", NULL);
            goto out;
        }
        wf_note("update: sha256 matches");
    }
    s_act = "Unpacking...";
    if (unpack() < 0) goto out;

    /* the swap: the running XLT keeps its name with the version in it */
    s_act = "Putting the new XLT in place...";
    sceIoRemove(s_old); /* the same version installed before */
    int have_self = sceIoRename(s_self, s_old) >= 0;
    if (!have_self) {
        int64_t sz;
        if (file_size(s_self, &sz) == 0) { /* it is there but won't move */
            sceIoRemove(s_new);
            fail("Couldn't rename EBOOT.PBP in XLT's folder", NULL);
            goto out;
        }
    }
    if (sceIoRename(s_new, s_self) < 0) {
        if (have_self) sceIoRename(s_old, s_self);
        sceIoRemove(s_new);
        fail("Couldn't put the new EBOOT.PBP in place", NULL);
        goto out;
    }
    if (!have_self) s_backup[0] = 0;
    sceIoRemove(s_zip);
    wf_note("update: installed %s, old one kept as %s", s_rel.version, have_self ? s_old : "(none)");
    s_act = "Updating ARK's start-up copy...";
    launcher_sync_now(); /* VBOOT.PBP follows when XLT starts at boot */
    s_known = 3;
    s_state = UP_INSTALLED;
out:
    free(s_buf);
    s_buf = NULL;
    sceIoRemove(s_zip);
}

int update_step(void)
{
    int ok;
    char msg[160];
    if (s_state == UP_CHECKING && wf_fetch_done(&ok, msg, sizeof(msg))) {
        if (!ok && strstr(msg, "(403)")) fail("GitHub is limiting checks from this network: try again in an hour", NULL);
        else if (!ok && strstr(msg, "(404)")) fail("GitHub has no XLT release to offer", NULL);
        else if (!ok) fail("%s", msg);
        else read_answer();
        return 1;
    }
    if (s_state == UP_DOWNLOADING && wf_fetch_done(&ok, msg, sizeof(msg))) {
        if (!ok) {
            fail("%s", msg);
        } else {
            s_act = "Checking the download...";
            s_state = UP_INSTALLING;
            wf_disconnect(); /* nothing more to fetch */
            bg_post(job_install, NULL, 0);
        }
        return 1;
    }
    return update_busy(); /* progress moves */
}
