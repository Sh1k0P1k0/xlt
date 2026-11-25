/* Renders Settings > About on the PC: the real settings.c (gfx.c through the
 * software GE of saver_preview.c), with the free-space count stood in for, so
 * the dialog can be seen: the question, Not now, "please wait" before the
 * driver's count, what it found, the Memory Stick row measuring again, and About
 * after. Checks the dialog closes and the rows update.
 * Writes PPM frames.
 *
 *   settings_preview OUTDIR [THEME] */
#define main saver_main
#include "saver_preview.c"
#undef main

#include "../src/gamecfg.h"
#include "../src/sysctl.h"
#include "../src/usbsync.h"
int usbsync_active(void) { return 0; }
int usbsync_start(void) { return -1; }
void usbsync_stop(void) {}
static int s_cast_calls, s_cast_last = -1;
void castplug_apply(int on) { s_cast_calls++; s_cast_last = on; }
static int s_net_calls, s_net_last = -1;
void netplug_apply(int on) { s_net_calls++; s_net_last = on; }
static int s_crash_calls, s_crash_last = -1;
void crashplug_apply(int on) { s_crash_calls++; s_crash_last = on; }
const char *usbsync_error(void) { return ""; }
int usbsync_attached(void) { return 0; }
int usbsync_talking(void) { return 0; }
int usbsync_progress(char *o, int n) { (void)o; (void)n; return 0; }
#include "../src/library.h"
#include "../src/bg.h"
#include "../src/sfx.h"

/* ------------------------------------------------------------ stand-ins */
GlobalCfg g_cfg;
void bg_flush(void) {}
void bg_post(void (*fn)(void *), const void *arg, int size) { (void)fn; (void)arg; (void)size; }
void globalcfg_save(void) {}
void history_clear(void) {}
int history_count(void) { return 0; }
int library_count(int v) { (void)v; return 0; }
static char s_note[160];
void library_note(const char *m) { snprintf(s_note, sizeof(s_note), "%s", m); printf("note: %s\n", m); }
void library_rescan(void) {}
void library_resort(void) {}
int library_scanning(void) { return 0; }
void saver_start(void) {}
void btntest_open(void) {}
void sfx_play(int id) { (void)id; }
void ui_toast(const char *m) { printf("toast: %s\n", m); }
void ui_library_changed(void) {}
void ui_open_removed(void) {}
void ui_arrange_home(void) {}
void ui_draw_status_bar(const char *t, const SysStatus *st) { (void)st; gfx_text(FONT_SMALL, 12, 5, t ? t : "Sun 22 Nov", C_MUTED); }
/* nuMB (N4): the bar as a stand-in (the real one is in ui.c): Settings in the focus box */
void ui_numb_draw_bar(void)
{
    gfx_rrect(NUMB_FOCUS_X - NUMB_BOX / 2, NUMB_BAR_CY - NUMB_BOX / 2, NUMB_BOX, NUMB_BOX, NUMB_BOX * 24 / 100, C_SURFACE);
    gfx_icon(ICON_SETTINGS, NUMB_FOCUS_X - 14, NUMB_BAR_CY - 14, 28, 28, C_TEXT);
    gfx_ring(NUMB_FOCUS_X - NUMB_BOX / 2 - 2, NUMB_BAR_CY - NUMB_BOX / 2 - 2, NUMB_BOX + 4, NUMB_BOX + 4, C_FOCUS);
    gfx_icon(ICON_WIFI, NUMB_FOCUS_X - NUMB_PITCH - 13, NUMB_BAR_CY - 13, 26, 26, C_MUTED);
}
void ui_draw_footer_hints(const Hint *h, int n) { ui_draw_hints(h, n, SCREEN_W / 2, 253); }
void format_size(char *b, int n, uint64_t bytes)
{
    unsigned tenths = (unsigned)(bytes * 10 / (1024ull * 1024 * 1024));
    snprintf(b, n, "%u.%u GB", tenths / 10, tenths % 10);
}
void ui_draw_hints(const Hint *hints, int n, int cx, int y)
{
    int total = (n - 1) * 16;
    for (int i = 0; i < n; i++)
        total += (hints[i].icon >= 0 ? 12 : gfx_text_width(FONT_SMALL, hints[i].key)) + 4 + gfx_text_width(FONT_SMALL, hints[i].label);
    int x = cx - total / 2;
    for (int i = 0; i < n; i++) {
        const Hint *h = &hints[i];
        if (h->icon >= 0) { gfx_icon(h->icon, x, y + 3, 12, 12, C_TEXT); x += 16; }
        else x = gfx_text(FONT_SMALL, x, y, h->key, C_TEXT) + 4;
        x = gfx_text(FONT_SMALL, x, y, h->label, C_MUTED) + 16;
    }
}
/* ARK, plugins, the registry: nothing there */
#include "../src/arkcfg.h"
#include "../src/plugins.h"
#include "../src/ark.h"
int ark_load_cfg(ArkCfg *c, char *e, int cap) { (void)c; (void)e; (void)cap; return -1; }
int ark_save_cfg(const ArkCfg *c, const char *e) { (void)c; (void)e; return -1; }
int ark_version(void) { return 4; }
void arkcfg_set(ArkCfg *c, int k, int v) { (void)c; (void)k; (void)v; }
int plugins_global(PlugLine *p, int max) { (void)p; (void)max; return 0; }
int plugins_global_set(const PlugLine *p, const char *r, int on) { (void)p; (void)r; (void)on; return -1; }
int psreg_get_int(const char *d, const char *k, int *v) { (void)d; (void)k; (void)v; return -1; }
int psreg_get_str(const char *d, const char *k, char *v, int n) { (void)d; (void)k; (void)v; (void)n; return -1; }
int psreg_set_int(const char *d, const char *k, int v) { (void)d; (void)k; (void)v; return -1; }
void system_read_formats(void) {}
void launcher_sync_now(void) {} /* arklauncher.c: the ARK folder isn't there on the PC */
int sysctl_impose_set(int p, int v) { (void)p; (void)v; return -1; }
void sysctl_firmware(char *b, int n) { snprintf(b, n, "6.61"); }
int sysctl_model(void) { return 10; }
int sysctl_usb_active(void) { return 0; }
const char *sysctl_model_name(void) { return "PSP-E1004 (Street)"; }

/* the count: driven by hand below */
static int s_driver_calls;
static uint64_t s_free, s_total;
int sysctl_storage(const char *dev, uint64_t *f, uint64_t *t)
{
    if (dev[0] == 'e' || !s_total) return -1;
    *f = s_free; *t = s_total;
    return 0;
}
int sysctl_storage_date(void) { return s_total ? 20261112 : 0; }
int sysctl_storage_measure(void) { s_driver_calls++; s_free = 47ull << 30; s_total = 119ull << 30; return 12051; }
int sysctl_storage_pause_s(void) { return s_driver_calls ? 0 : 12; }

/* update.c, stood in for: the dialog's states are set by the test */
#include "../src/update.h"
static int s_ust, s_uknown, s_ucalls[4]; /* check, install, stop, dismiss */
static UpdRelease s_urel = {.tag = "v1.2.0", .version = "1.2.0", .zip_size = 2093210};
static const char *s_unotes = "A made-up release, to show the dialog.\n\nWhat's new\n\n- Something new in Music.\n"
                              "- The old XLT is kept as EBOOT-1.1.0.PBP.\n"
                              "- Fixes for a few rare cases.\n- A very long line to see the wrapping and "
                              "the scroll marks work as they should at the dialog's width.\n- Eight\n- Nine";
static int64_t s_ugot;
void update_check(void) { s_ucalls[0]++; s_ust = UP_CHECKING; }
void update_install(void) { s_ucalls[1]++; s_ust = UP_DOWNLOADING; }
void update_stop(void) { s_ucalls[2]++; }
void update_dismiss(void) { s_ucalls[3]++; if (s_ust != UP_INSTALLED) s_ust = UP_IDLE; }
int update_step(void) { return 0; }
int update_state(void) { return s_ust; }
int update_busy(void) { return s_ust == UP_CHECKING || s_ust == UP_DOWNLOADING || s_ust == UP_INSTALLING; }
const UpdRelease *update_release(void) { return &s_urel; }
const char *update_notes(void) { return s_unotes; }
const char *update_error(void) { return "The server refused the file (503)"; }
const char *update_activity(void)
{
    return s_ust == UP_INSTALLING ? "Checking the download..." : s_ust == UP_CHECKING ? "Connecting to Wi-Fi..." : "From GitHub, over Wi-Fi";
}
void update_progress(int64_t *g, int64_t *t, int *k) { *g = s_ugot; *t = s_urel.zip_size; *k = 143; }
const char *update_backup(void) { return "EBOOT-1.1.0.PBP"; }
int update_known(void) { return s_uknown; }
int update_from_usb(void) { return 0; }
static int s_urestart;
void update_request_restart(void) { s_urestart = 1; }
int update_take_restart(void) { return 0; }

#include "../src/settings.c"

/* ---------------------------------------------------------------- frames */
static SysStatus s_st = {21, 41, 76, 0, 1};
static const char *s_dir;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL "); printf(__VA_ARGS__); puts(""); fails++; } } while (0)

static void frames(int n) { for (int i = 0; i < n; i++) settings_step(); }
static void shot(const char *name)
{
    gfx_begin(g_th->bg);
    settings_draw(&s_st);
    gfx_end();
    save(s_dir, name);
}
static void go_about(void) /* from the sections column */
{
    if (S.col == 1) settings_input(PSP_CTRL_CIRCLE);
    for (int i = 0; i < C_COUNT && S.cat != C_ABOUT; i++) settings_input(PSP_CTRL_UP);
}

/* XLT_DEMO=DIR: Settings opening, the theme changed live, About counting; every other frame saved */
static int s_mf, s_tk;
static void dframe(const char *dir, unsigned b)
{
    if (b) settings_input(b);
    settings_step();
    if (++s_tk & 1) return;
    gfx_begin(g_th->bg);
    settings_draw(&s_st);
    gfx_end();
    char name[16];
    snprintf(name, sizeof(name), "f%05d", s_mf++);
    save(dir, name);
}
static void dwait(const char *dir, unsigned b, int n) { dframe(dir, b); for (int i = 1; i < n; i++) dframe(dir, 0); }
static int demo(const char *dir)
{
    settings_open();
    dwait(dir, 0, 40);
    dwait(dir, PSP_CTRL_RIGHT, 30);                    /* into XLT: Theme */
    for (int i = 0; i < 4; i++) dwait(dir, PSP_CTRL_RIGHT, 32); /* the themes, live */
    for (int i = 0; i < 4; i++) dwait(dir, PSP_CTRL_LEFT, 8);   /* back to Night */
    dwait(dir, PSP_CTRL_CIRCLE, 16);
    for (int i = 0; i < 4; i++) dwait(dir, PSP_CTRL_DOWN, 14);  /* the sections */
    dwait(dir, PSP_CTRL_UP, 10);
    for (int i = 0; i < 4; i++) dwait(dir, PSP_CTRL_UP, 10);    /* ...round to About */
    dframe(dir, 0);
    if (S.cat != C_ABOUT) printf("settings demo: ended on section %d, not About\n", S.cat);
    dwait(dir, 0, 24);
    dwait(dir, PSP_CTRL_RIGHT, 110);                   /* in: the note, the count, then the result */
    printf("settings demo: %d frames\n", s_mf);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    s_dir = argv[1];
    if (getenv("XLT_DEMO")) {
        dm = gfx_dither_matrix;
        sceGuTexMode(GU_PSM_4444, 0, 0, GU_TRUE);
        sceGuTexImage(0, ATLAS_W, ATLAS_H, ATLAS_W, NULL);
        theme_set(0);
        return demo(getenv("XLT_DEMO"));
    }
    dm = gfx_dither_matrix;
    sceGuTexMode(GU_PSM_4444, 0, 0, GU_TRUE);
    sceGuTexImage(0, ATLAS_W, ATLAS_H, ATLAS_W, NULL);
    theme_set(argc > 2 ? atoi(argv[2]) : 0);

    settings_open();
    frames(40);
    go_about();                       /* ↑ from XLT wraps to About: only highlighted, nothing counted */
    frames(20);
    CHECK(s_space == SP_IDLE, "a count on only highlighting About");
    shot("set_0_highlighted");
    /* entering About asks first; Not now (○) keeps you in About, and it doesn't ask again */
    settings_input(PSP_CTRL_RIGHT);
    frames(30);
    CHECK(s_space == SP_ASK && s_driver_calls == 0, "entering About: state %d, calls %d", s_space, s_driver_calls);
    shot("set_1_ask");
    settings_input(PSP_CTRL_RTRIGGER); /* nothing gets past the question */
    CHECK(S.cat == C_ABOUT, "L/R moved under the question");
    settings_input(PSP_CTRL_CIRCLE);
    frames(30);
    CHECK(s_space == SP_IDLE && s_space_t.x == 0 && s_driver_calls == 0 && S.open && S.col == 1,
          "Not now: state %d, calls %d, open %d, col %d", s_space, s_driver_calls, S.open, S.col);
    settings_input(PSP_CTRL_CIRCLE);
    settings_input(PSP_CTRL_RIGHT);
    frames(20);
    CHECK(s_space == SP_IDLE, "asked again on entering About (state %d)", s_space);
    /* the Memory Stick row: Cross asks, Cross measures */
    {
        Cat *ab = &s_cats[C_ABOUT];
        int row = -1;
        for (int i = 0; i < ab->n; i++)
            if (ab->items[i].src == SRC_ACTION && ab->items[i].key == A_SPACE) row = i;
        CHECK(row >= 0, "no Memory Stick row to measure with");
        if (row >= 0) {
            S.sel[C_ABOUT] = row;
            if (S.top[C_ABOUT] > row) S.top[C_ABOUT] = row;
            if (row >= S.top[C_ABOUT] + VISIBLE) S.top[C_ABOUT] = row - VISIBLE + 1;
        }
    }
    frames(5);
    shot("set_2_row_not_measured");
    settings_input(PSP_CTRL_CROSS);
    frames(30);
    CHECK(s_space == SP_ASK, "Cross on the row didn't ask (state %d)", s_space);
    settings_input(PSP_CTRL_CROSS);
    frames(1);
    CHECK(s_space == SP_SCAN && s_driver_calls == 0 && s_space_t.x >= FX(1), "Measure: state %d, calls %d", s_space, s_driver_calls);
    shot("set_3_scanning");
    frames(5);
    CHECK(s_driver_calls == 1 && s_space == SP_DONE, "driver count: %d calls, state %d", s_driver_calls, s_space);
    CHECK(strstr(s_note, "free space measured") && strstr(s_note, "first time"), "no log line: %s", s_note);
    frames(20);
    shot("set_4_done");
    settings_input(PSP_CTRL_CROSS); /* OK */
    frames(30);
    CHECK(s_space == SP_IDLE && s_space_t.x == 0 && S.open && S.cat == C_ABOUT, "OK didn't close it (state %d)", s_space);
    shot("set_5_about_after");
    /* the driver counted: the row measures at once, no dialog */
    settings_input(PSP_CTRL_CROSS);
    frames(2);
    CHECK(s_space == SP_IDLE && s_driver_calls == 2 && strstr(s_note, "again"), "instant count: state %d, calls %d", s_space, s_driver_calls);
    /* the done dialog also goes by itself */
    s_driver_calls = 0; s_counted = 0;
    settings_input(PSP_CTRL_CIRCLE);
    settings_input(PSP_CTRL_RIGHT);
    frames(20);
    settings_input(PSP_CTRL_CROSS);
    frames(200);
    CHECK(s_space == SP_IDLE && s_driver_calls == 1, "done didn't close by itself (state %d, calls %d)", s_space, s_driver_calls);

    /* Check for updates: each state of its dialog */
    {
        Cat *ab = &s_cats[C_ABOUT];
        int row = -1;
        for (int i = 0; i < ab->n; i++)
            if (ab->items[i].src == SRC_ACTION && ab->items[i].key == A_UPDATE) row = i;
        CHECK(row >= 0, "no Check for updates row in About");
        if (row >= 0) {
            S.sel[C_ABOUT] = row;
            if (S.top[C_ABOUT] > row) S.top[C_ABOUT] = row;
            if (row >= S.top[C_ABOUT] + VISIBLE) S.top[C_ABOUT] = row - VISIBLE + 1;
            frames(5);
            shot("upd_0_row");
            settings_input(PSP_CTRL_CROSS);
            frames(30);
            CHECK(s_ucalls[0] == 1 && s_upd_open, "✕ didn't start a check");
            shot("upd_1_checking");
            settings_input(PSP_CTRL_RTRIGGER); /* nothing gets past the dialog */
            CHECK(S.cat == C_ABOUT, "L/R moved under the dialog");
            s_ust = UP_LATEST; s_uknown = 1;
            frames(5);
            shot("upd_2_latest");
            settings_input(PSP_CTRL_CROSS);
            frames(30);
            CHECK(!s_upd_open && s_ucalls[3] == 1 && S.open && S.cat == C_ABOUT, "✕ didn't close Up to date");
            shot("upd_3_row_up_to_date");
            settings_input(PSP_CTRL_CROSS);
            frames(10);
            s_ust = UP_AVAILABLE; s_uknown = 2;
            frames(30);
            shot("upd_4_available");
            CHECK(s_upd_nlines > UPD_SHOWN, "the notes weren't wrapped (%d lines)", s_upd_nlines);
            for (int i = 0; i < 30; i++) settings_input(PSP_CTRL_DOWN);
            frames(2);
            CHECK(s_upd_top == s_upd_nlines - UPD_SHOWN, "notes scroll: top %d of %d", s_upd_top, s_upd_nlines);
            shot("upd_5_available_scrolled");
            settings_input(PSP_CTRL_CROSS);
            CHECK(s_ucalls[1] == 1 && s_ust == UP_DOWNLOADING, "✕ didn't install");
            s_ugot = 812345;
            frames(5);
            shot("upd_6_downloading");
            s_ust = UP_INSTALLING;
            frames(2);
            settings_input(PSP_CTRL_CIRCLE);
            CHECK(s_upd_open, "○ closed the dialog while installing");
            shot("upd_7_installing");
            s_ust = UP_INSTALLED; s_uknown = 3;
            frames(5);
            shot("upd_8_installed");
            settings_input(PSP_CTRL_CIRCLE);
            frames(30);
            CHECK(!s_upd_open && s_ust == UP_INSTALLED, "Later lost the installed state");
            shot("upd_9_row_restart");
            settings_input(PSP_CTRL_CROSS);
            frames(10);
            CHECK(s_ucalls[0] == 2 - 0 && s_upd_open, "installed: the dialog didn't open again (checks %d)", s_ucalls[0]);
            settings_input(PSP_CTRL_CROSS);
            CHECK(s_urestart && !S.open, "Restart now didn't ask for a restart and close Settings");
            /* a failure */
            settings_open();
            frames(40);
            s_ust = UP_FAILED;
            s_upd_open = 1; spring_snap(&s_upd_t, FX(1));
            frames(3);
            shot("upd_10_failed");
            settings_input(PSP_CTRL_CIRCLE);
            CHECK(!s_upd_open, "○ didn't close the failure");
        }
    }

    { /* Sound > Crossfade: Off -> 2 -> ... -> 10 s -> Off, saved to xlt.ini when Settings closes */
        settings_open();
        frames(40);
        if (S.col == 1) settings_input(PSP_CTRL_CIRCLE);
        for (int i = 0; i < C_COUNT && S.cat != C_SOUND; i++) settings_input(PSP_CTRL_DOWN);
        CHECK(S.cat == C_SOUND, "didn't reach Sound (at %d)", S.cat);
        settings_input(PSP_CTRL_CROSS);
        Cat *snd = &s_cats[C_SOUND];
        int row = -1;
        for (int i = 0; i < snd->n; i++)
            if (!strcmp(snd->items[i].label, "Crossfade")) row = i;
        CHECK(row >= 0, "no Crossfade row in Sound");
        if (row >= 0) {
            S.sel[C_SOUND] = row;
            if (S.top[C_SOUND] > row) S.top[C_SOUND] = row;
            if (row >= S.top[C_SOUND] + VISIBLE) S.top[C_SOUND] = row - VISIBLE + 1;
            g_cfg.crossfade = 0;
            char v[32];
            item_value(&snd->items[row], v, sizeof(v));
            CHECK(!strcmp(v, "Off"), "crossfade off reads \"%s\"", v);
            for (int i = 0; i < 3; i++) settings_input(PSP_CTRL_RIGHT);
            item_value(&snd->items[row], v, sizeof(v));
            CHECK(g_cfg.crossfade == 6 && !strcmp(v, "6 s"), "three steps right: %d (\"%s\")", g_cfg.crossfade, v);
            frames(20);
            shot("snd_0_crossfade_6s");
            for (int i = 0; i < 4; i++) settings_input(PSP_CTRL_RIGHT);
            CHECK(g_cfg.crossfade == 2, "past 10 s it should wrap to Off, then 2 s: %d", g_cfg.crossfade);
            settings_input(PSP_CTRL_LEFT);
            settings_input(PSP_CTRL_LEFT);
            CHECK(g_cfg.crossfade == 10, "left from 2 s: Off, then 10 s: %d", g_cfg.crossfade);
            g_cfg.crossfade = 0;
        }
    }

    { /* XLT > XLT Cast: Off -> On installs and switches the plugin (castplug_apply) */
        if (S.col == 1) settings_input(PSP_CTRL_CIRCLE);
        for (int i = 0; i < C_COUNT && S.cat != C_XLT; i++) settings_input(PSP_CTRL_UP);
        CHECK(S.cat == C_XLT, "didn't reach XLT (at %d)", S.cat);
        settings_input(PSP_CTRL_CROSS);
        Cat *x = &s_cats[C_XLT];
        int row = -1;
        for (int i = 0; i < x->n; i++)
            if (x->items[i].src == SRC_XLT && x->items[i].key == X_CAST) row = i;
        CHECK(row >= 0, "no XLT Cast row in XLT");
        if (row >= 0) {
            S.sel[C_XLT] = row;
            if (S.top[C_XLT] > row) S.top[C_XLT] = row;
            if (row >= S.top[C_XLT] + VISIBLE) S.top[C_XLT] = row - VISIBLE + 1;
            g_cfg.cast = 0;
            char v[32];
            item_value(&x->items[row], v, sizeof(v));
            CHECK(!strcmp(v, "Off"), "cast off reads \"%s\"", v);
            settings_input(PSP_CTRL_RIGHT);
            item_value(&x->items[row], v, sizeof(v));
            CHECK(g_cfg.cast == 1 && !strcmp(v, "On") && s_cast_calls == 1 && s_cast_last == 1,
                  "turned on: cast %d (\"%s\"), %d calls, last %d", g_cfg.cast, v, s_cast_calls, s_cast_last);
            frames(20);
            shot("cast_0_on");
            settings_input(PSP_CTRL_RIGHT); /* already on: right does nothing */
            CHECK(s_cast_calls == 1, "right on an On switch changed it (%d calls)", s_cast_calls);
            settings_input(PSP_CTRL_LEFT);
            CHECK(g_cfg.cast == 0 && s_cast_calls == 2 && s_cast_last == 0, "off again: %d, last %d", g_cfg.cast, s_cast_last);
        }
        /* XLT > Crash log: on by default; off and on again through crashplug_apply */
        int crow = -1;
        for (int i = 0; i < x->n; i++)
            if (x->items[i].src == SRC_XLT && x->items[i].key == X_CRASH) crow = i;
        CHECK(crow >= 0, "no Crash log row in XLT");
        if (crow >= 0) {
            S.sel[C_XLT] = crow;
            if (S.top[C_XLT] > crow) S.top[C_XLT] = crow;
            if (crow >= S.top[C_XLT] + VISIBLE) S.top[C_XLT] = crow - VISIBLE + 1;
            char v[32];
            item_value(&x->items[crow], v, sizeof(v));
            g_cfg.crash_log = 1; /* (the default lives in gamecfg.c, which this test does without) */
            item_value(&x->items[crow], v, sizeof(v));
            CHECK(!strcmp(v, "On"), "crash log on reads \"%s\"", v);
            frames(20);
            shot("crash_0_on");
            settings_input(PSP_CTRL_LEFT);
            CHECK(g_cfg.crash_log == 0 && s_crash_calls == 1 && s_crash_last == 0, "off: %d, %d calls, last %d",
                  g_cfg.crash_log, s_crash_calls, s_crash_last);
            settings_input(PSP_CTRL_RIGHT);
            CHECK(g_cfg.crash_log == 1 && s_crash_calls == 2 && s_crash_last == 1, "on again: %d, last %d",
                  g_cfg.crash_log, s_crash_last);
        }
        /* Network > Internet over USB: the same, through netplug_apply */
        x = &s_cats[C_NETWORK];
        S.cat = C_NETWORK;
        int nrow = -1;
        for (int i = 0; i < x->n; i++)
            if (x->items[i].src == SRC_XLT && x->items[i].key == X_NET) nrow = i;
        CHECK(nrow >= 0, "no Internet over USB row in Network");
        if (nrow >= 0) {
            S.sel[C_NETWORK] = nrow;
            S.top[C_NETWORK] = 0;
            g_cfg.usb_net = 0;
            char v[32];
            item_value(&x->items[nrow], v, sizeof(v));
            CHECK(!strcmp(v, "Off"), "net off reads \"%s\"", v);
            settings_input(PSP_CTRL_RIGHT);
            item_value(&x->items[nrow], v, sizeof(v));
            CHECK(g_cfg.usb_net == 1 && !strcmp(v, "On") && s_net_calls == 1 && s_net_last == 1,
                  "net on: %d (\"%s\"), %d calls, last %d", g_cfg.usb_net, v, s_net_calls, s_net_last);
            frames(20);
            shot("net_0_on");
            settings_input(PSP_CTRL_LEFT);
            CHECK(g_cfg.usb_net == 0 && s_net_calls == 2 && s_net_last == 0, "net off again: %d, last %d",
                  g_cfg.usb_net, s_net_last);
        }
    }

    { /* nuMB (docs/numb.md, N4): a section as a column under the bar; ○ goes straight back to the bar */
        if (S.open && S.col == 1) settings_input(PSP_CTRL_CIRCLE);
        if (S.open) settings_input(PSP_CTRL_CIRCLE);
        frames(40);
        g_cfg.ui_mode = UI_NUMB;
        settings_open_section(C_XLT);
        frames(40);
        CHECK(S.col == 1, "nuMB: a section should open on its settings (col %d)", S.col);
        shot("numb_set_0_xlt");
        settings_input(PSP_CTRL_DOWN);
        settings_input(PSP_CTRL_DOWN);
        frames(20);
        shot("numb_set_1_down");
        for (int i = 0; i < 6; i++) settings_input(PSP_CTRL_DOWN); /* scrolled */
        frames(20);
        shot("numb_set_2_scrolled");
        settings_input(PSP_CTRL_RTRIGGER);
        frames(20);
        shot("numb_set_3_next_section");
        settings_input(PSP_CTRL_CIRCLE);
        CHECK(!S.open, "nuMB: ○ in a section should go back to the bar");
        frames(40);
        g_cfg.ui_mode = UI_XLT;
    }

    puts(fails ? "settings_preview: FAILED" : "settings_preview: ok");
    return fails != 0;
}
