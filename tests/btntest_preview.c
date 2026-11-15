/* Renders Settings > XLT > Button test on the PC: the real btntest.c (gfx.c through
 * the software GE of saver_preview.c), driven by a made-up pad: nothing pressed,
 * buttons held, the stick pushed round, every button seen, and ○ held to leave.
 * Checks the counts, that the opening ✕ isn't counted until let go, and that every
 * press is swallowed. Writes PPM frames.
 *
 *   btntest_preview OUTDIR */
#define main saver_main
#include "saver_preview.c"
#undef main

#include "../src/sfx.h"
#include "../src/sysctl.h"

/* ------------------------------------------------------------ stand-ins */
static unsigned s_btn, s_kbtn;
static int s_lx = 128, s_ly = 128;
int sceCtrlPeekBufferPositive(SceCtrlData *pad, int count)
{
    (void)count;
    pad->Buttons = s_btn;
    pad->Lx = (unsigned char)s_lx;
    pad->Ly = (unsigned char)s_ly;
    return 1;
}
unsigned sysctl_kernel_buttons(void) { return s_kbtn; }

int sysctl_model(void) { return 2; } /* a PSP-3000: every switch */
void sfx_play(int id) { (void)id; }
void ui_draw_status_bar(const char *t, const SysStatus *st) { (void)st; gfx_text(FONT_SMALL, 12, 5, t, C_MUTED); }
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

#include "../src/btntest.c"

/* ---------------------------------------------------------------- frames */
static SysStatus s_st = {21, 41, 76, 0, 1};
static const char *s_dir;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL "); printf(__VA_ARGS__); puts(""); fails++; } } while (0)

static int seen_count(void) { int n = 0; for (int i = 0; i < B_COUNT; i++) n += was_seen(i); return n; }
static void step(int n)
{
    for (int i = 0; i < n; i++) {
        Input in = {0xFFFFFFFFu, 0xFFFFFFFFu};
        btntest_update(&in);
        if (btntest_is_open() && B.open) CHECK(!in.pressed && !in.held, "a press got past the tester");
    }
}
static void shot(const char *name)
{
    gfx_begin(g_th->bg);
    btntest_draw(&s_st);
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

    s_btn = PSP_CTRL_CROSS; /* the ✕ that opened it, still down */
    btntest_open();
    step(30);
    CHECK(btntest_covers(), "not fully shown after 30 frames");
    CHECK(seen_count() == 0, "the opening ✕ was counted (%d)", seen_count());
    shot("bt_0_open");
    s_btn = 0;
    step(1);
    s_btn = PSP_CTRL_CROSS;
    step(1);
    CHECK(seen_count() == 1 && was_seen(B_CROSS), "✕ pressed again wasn't counted");
    /* a few held, the stick up-left, HOLD and headphones on */
    s_btn = PSP_CTRL_LTRIGGER | PSP_CTRL_UP | PSP_CTRL_TRIANGLE | PSP_CTRL_START | PSP_CTRL_HOLD;
    s_kbtn = PSP_CTRL_VOLUP | PSP_CTRL_REMOTE | PSP_CTRL_HOLD;
    s_lx = 30; s_ly = 60;
    step(2);
    CHECK(seen_count() == 6, "held: %d seen", seen_count());
    shot("bt_1_held");
    /* the stick round its edge: the range box grows */
    for (int a = 0; a < 360; a += 10) {
        double r = a * 3.14159265 / 180;
        s_lx = 128 + (int)(126 * cos(r)); s_ly = 128 + (int)(124 * sin(r));
        step(1);
    }
    s_lx = 131; s_ly = 125; /* let go, a little off centre */
    s_btn = 0; s_kbtn = 0;
    step(1);
    CHECK(B.minx < 5 && B.maxx > 250 && B.miny < 8 && B.maxy > 248, "range %d-%d %d-%d", B.minx, B.maxx, B.miny, B.maxy);
    shot("bt_2_range");
    /* everything */
    for (int i = 0; i < B_COUNT; i++) {
        if (MASK[i] & KERNEL_ONLY) s_kbtn = MASK[i]; else s_btn = MASK[i];
        step(1);
        s_btn = s_kbtn = 0;
        step(1);
    }
    CHECK(seen_count() == B_COUNT, "all: %d of %d", seen_count(), B_COUNT);
    s_kbtn = PSP_CTRL_WLAN_UP | PSP_CTRL_DISC;
    step(1);
    shot("bt_3_all");
    /* ○ held: the bar fills, then it closes */
    s_btn = PSP_CTRL_CIRCLE;
    step(45);
    CHECK(B.open, "closed before 1.5 s");
    shot("bt_4_leaving");
    step(46);
    CHECK(!B.open, "○ held 1.5 s didn't close it");
    step(40);
    CHECK(!btntest_is_open(), "still fading after 40 frames");
    /* ○ tapped doesn't close it */
    s_btn = 0;
    btntest_open();
    step(30);
    s_btn = PSP_CTRL_CIRCLE; step(20); s_btn = 0; step(5); s_btn = PSP_CTRL_CIRCLE; step(20);
    CHECK(B.open, "short ○ presses closed it");

    puts(fails ? "btntest_preview: FAILED" : "btntest_preview: ok");
    return fails != 0;
}
