/* Renders the UMD "Copy to Memory Stick" dialog on the PC: the real dumpdlg.c (gfx.c
 * through the software GE of saver_preview.c), with umddump.c stood in for. Checks it
 * takes every press, ○ asks before stopping (and ○/Keep copying goes back), Stop stops
 * and closes, a finished copy rescans the library, a failure stays up until ✕. Writes
 * PPM frames.
 *
 *   dumpdlg_preview OUTDIR */
#define main saver_main
#include "saver_preview.c"
#undef main

#include "../src/sfx.h"
#include "../src/umddump.h"

/* ------------------------------------------------------------ stand-ins */
static DumpStatus s_st;
static int s_started, s_stopped, s_rescans, s_refuse;
int umddump_start(const Entry *disc, const char **why)
{
    (void)disc;
    if (s_refuse) { *why = "A disc is being copied already"; return -1; }
    s_started++;
    memset(&s_st, 0, sizeof(s_st));
    s_st.state = DUMP_PREPARING;
    return 0;
}
void umddump_status(DumpStatus *out) { *out = s_st; }
void umddump_stop(void) { s_stopped++; }
void umddump_clear(void) { s_st.state = DUMP_IDLE; }
void library_rescan(void) { s_rescans++; }
static char s_toast[128];
void ui_toast(const char *m) { snprintf(s_toast, sizeof(s_toast), "%s", m); }
void sfx_play(int id) { (void)id; }
void format_size(char *b, int n, uint64_t bytes)
{
    if (bytes >= 1024ull * 1024 * 1024) {
        unsigned tenths = (unsigned)(bytes * 10 / (1024ull * 1024 * 1024));
        snprintf(b, n, "%u.%u GB", tenths / 10, tenths % 10);
    } else if (bytes >= 1024 * 1024) {
        snprintf(b, n, "%u MB", (unsigned)(bytes / (1024 * 1024)));
    } else {
        snprintf(b, n, "%u KB", (unsigned)((bytes + 1023) / 1024));
    }
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

#include "../src/dumpname.c"
#include "../src/dumpdlg.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL "); printf(__VA_ARGS__); puts(""); fails++; } } while (0)
static const char *s_dir;
static unsigned s_left;

static void dframe(unsigned b)
{
    static unsigned prev;
    Input in = {b, b & ~prev};
    prev = b;
    dumpdlg_update(&in);
    s_left = in.pressed;
}
static void tap(unsigned b) { dframe(b); unsigned got = s_left; dframe(0); s_left = got; }
static void frames(int n) { for (int i = 0; i < n; i++) dframe(0); }
static void shot(const char *name)
{
    gfx_begin(g_th->bg);
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BG);
    gfx_text(FONT_BODY, 20, 40, "The UMD card under it", C_TEXT);
    dumpdlg_draw();
    gfx_end();
    save(s_dir, name);
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    s_dir = argv[1];
    dm = gfx_dither_matrix;
    sceGuTexMode(GU_PSM_4444, 0, 0, GU_TRUE);
    sceGuTexImage(0, ATLAS_W, ATLAS_H, ATLAS_W, NULL);
    theme_set(argc > 2 ? atoi(argv[2]) : 0);

    Entry disc;
    memset(&disc, 0, sizeof(disc));
    snprintf(disc.title, sizeof(disc.title), "Star Rally Turbo: Coastline Edition");
    snprintf(disc.id, sizeof(disc.id), "ULES09999");
    const uint64_t MB = 1 << 20;

    dumpdlg_open(&disc);
    CHECK(s_started == 1 && D.open, "didn't start and open");
    frames(30);
    shot("dump_0_preparing");
    tap(PSP_CTRL_TRIANGLE);
    CHECK(!s_left, "a press got past the dialog");

    s_st.state = DUMP_COPYING;
    s_st.total = 1434 * MB;
    snprintf(s_st.name, sizeof(s_st.name), "Star Rally Turbo Coastline Edition [ULES09999].iso");
    s_st.done = 3 * MB; s_st.elapsed_ms = 1500;
    frames(2);
    shot("dump_1_starting");
    s_st.done = 412 * MB; s_st.elapsed_ms = 180000;
    frames(2);
    shot("dump_2_copying");

    tap(PSP_CTRL_CIRCLE);
    CHECK(D.asking && !D.stop_yes, "○ didn't ask (on Keep copying)");
    frames(3);
    shot("dump_3_ask");
    tap(PSP_CTRL_CROSS);
    CHECK(!D.asking && !s_stopped, "✕ on Keep copying stopped it");
    tap(PSP_CTRL_CIRCLE); tap(PSP_CTRL_CIRCLE);
    CHECK(!D.asking && !s_stopped, "○ in the question didn't go back");
    tap(PSP_CTRL_CIRCLE); tap(PSP_CTRL_LEFT);
    CHECK(D.stop_yes, "← didn't move to Stop");
    frames(3);
    shot("dump_4_ask_stop");
    tap(PSP_CTRL_CROSS);
    CHECK(s_stopped == 1, "Stop didn't stop");
    s_st.state = DUMP_STOPPED;
    frames(30);
    CHECK(!dumpdlg_is_open(), "didn't close after stopping");
    CHECK(!s_rescans, "rescanned after a stop");

    /* A finished copy */
    dumpdlg_open(&disc);
    s_st.state = DUMP_COPYING; s_st.total = 1434 * MB; s_st.done = 1000 * MB; s_st.elapsed_ms = 400000;
    snprintf(s_st.name, sizeof(s_st.name), "Star Rally Turbo Coastline Edition [ULES09999].iso");
    frames(5);
    s_st.state = DUMP_DONE; s_st.done = s_st.total; s_st.elapsed_ms = 574000;
    frames(30);
    CHECK(s_rescans == 1, "no rescan after the copy");
    shot("dump_5_done");
    tap(PSP_CTRL_CROSS);
    frames(30);
    CHECK(!dumpdlg_is_open(), "✕ didn't close the finished dialog");

    /* A failure */
    dumpdlg_open(&disc);
    s_st.state = DUMP_COPYING; s_st.total = 1434 * MB; s_st.done = 700 * MB; s_st.elapsed_ms = 300000;
    frames(3);
    s_st.state = DUMP_FAILED;
    snprintf(s_st.error, sizeof(s_st.error), "A part of the disc can't be read (at 700 MB). Clean the disc and try again.");
    frames(30);
    shot("dump_6_failed");
    s_st.state = DUMP_FAILED;
    snprintf(s_st.error, sizeof(s_st.error), "Not enough space: the disc needs 1.4 GB, the Memory Stick has 812 MB free.");
    D.st.state = DUMP_FAILED;
    frames(2);
    shot("dump_7_no_space");
    tap(PSP_CTRL_TRIANGLE);
    CHECK(D.open, "△ closed the failure");
    tap(PSP_CTRL_CROSS);
    frames(30);
    CHECK(!dumpdlg_is_open(), "✕ didn't close the failure");

    /* Refused to start: a toast, no dialog */
    s_refuse = 1;
    dumpdlg_open(&disc);
    CHECK(!D.open && s_toast[0], "a refused start opened the dialog");

    puts(fails ? "dumpdlg_preview: FAILED" : "dumpdlg_preview: ok");
    return fails != 0;
}
