/* Renders XLT's HOME dialog on the PC: the real quitdlg.c (gfx.c through the software GE
 * of saver_preview.c), with HOME and the system dialog stood in for. Checks HOME opens it
 * on Yes, ←/→ switch, No / ○ / HOME close it, Yes asks XLT to quit, the button test keeps
 * it shut, and with the system dialog still on (the call failed) it never opens. Writes PPM
 * frames.
 *
 *   quitdlg_preview OUTDIR */
#define main saver_main
#include "saver_preview.c"
#undef main

#include "../src/sfx.h"
#include "../src/sysctl.h"

/* ------------------------------------------------------------ stand-ins */
static unsigned s_kbtn;
static int s_popup_off, s_popup_works = 1, s_exit, s_btntest;
int sysctl_home_popup(int on) { if (!s_popup_works) return -1; s_popup_off = !on; return 0; }
void sysctl_home_alive(void) {}
int sysctl_home_popup_off(void) { return s_popup_off; }
unsigned sysctl_kernel_buttons(void) { return s_kbtn; }
int btntest_is_open(void) { return s_btntest; }
void library_note(const char *m) { printf("note: %s\n", m); }
void sfx_play(int id) { (void)id; }
void ui_request_exit(void) { s_exit++; }
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

#include "../src/quitdlg.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL "); printf(__VA_ARGS__); puts(""); fails++; } } while (0)
static const char *s_dir;
static unsigned s_left; /* what reached the rest of XLT on the last frame */

/* One frame as main.c runs it: pad buttons b, HOME held or not. */
static void qframe(unsigned b, int home)
{
    static unsigned prev;
    s_kbtn = home ? PSP_CTRL_HOME : 0;
    Input in = {b, b & ~prev};
    prev = b;
    quitdlg_poll(&in);
    quitdlg_update(&in);
    s_left = in.pressed;
}
static void tap(unsigned b) { qframe(b, 0); unsigned got = s_left; qframe(0, 0); s_left = got; }
static void home(void) { qframe(0, 1); qframe(0, 0); }
static void frames(int n) { for (int i = 0; i < n; i++) qframe(0, 0); }
/* XLT_DEMO_BG=PPM (480x272): a screen to draw the dialog over, for the README's still */
static float s_bg[SCREEN_H][SCREEN_W][3];
static int s_has_bg;
static void load_bg(const char *p)
{
    FILE *f = p ? fopen(p, "rb") : NULL;
    int w, h, m;
    if (!f || fscanf(f, "P6 %d %d %d", &w, &h, &m) != 3 || w != SCREEN_W || h != SCREEN_H) { if (f) fclose(f); return; }
    fgetc(f);
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            for (int k = 0; k < 3; k++) s_bg[y][x][k] = fgetc(f) / 255.f;
    fclose(f);
    s_has_bg = 1;
}
static void shot(const char *name)
{
    gfx_begin(g_th->bg);
    if (s_has_bg) {
        memcpy(fb, s_bg, sizeof(fb));
    } else {
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BG);
        gfx_text(FONT_BODY, 20, 40, "Whatever XLT shows under it", C_TEXT);
    }
    quitdlg_draw();
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
    load_bg(getenv("XLT_DEMO_BG"));

    frames(2);
    CHECK(s_popup_off, "the system dialog wasn't turned off");
    tap(PSP_CTRL_CROSS);
    CHECK(s_left == PSP_CTRL_CROSS, "a press didn't get through with the dialog shut");
    home();
    CHECK(Q.open && Q.yes, "HOME didn't open it on Yes");
    frames(30);
    shot("q_0_yes");
    tap(PSP_CTRL_RIGHT);
    CHECK(!Q.yes, "→ didn't move to No");
    frames(5);
    shot("q_1_no");
    tap(PSP_CTRL_TRIANGLE);
    CHECK(Q.open && !s_left, "a press got past the dialog");
    tap(PSP_CTRL_CROSS);
    CHECK(!Q.open && !s_exit, "✕ on No didn't just close it");
    frames(30);
    CHECK(!quitdlg_is_open(), "still fading");
    home(); tap(PSP_CTRL_CIRCLE);
    CHECK(!Q.open && !s_exit, "○ didn't cancel");
    home(); home();
    CHECK(!Q.open && !s_exit, "HOME again didn't cancel");
    home(); tap(PSP_CTRL_LEFT); tap(PSP_CTRL_LEFT); tap(PSP_CTRL_CROSS);
    CHECK(s_exit == 1, "✕ on Yes didn't quit");
    frames(30);
    /* HOME held: one press, not one a frame */
    qframe(0, 1); qframe(0, 1); qframe(0, 1);
    CHECK(Q.open, "HOME held didn't open it");
    CHECK(Q.yes, "HOME held kept toggling it");
    tap(PSP_CTRL_CIRCLE);
    CHECK(!Q.open, "○ didn't close it");
    /* the button test keeps HOME */
    s_btntest = 1; home(); s_btntest = 0;
    CHECK(!Q.open, "opened over the button test");
    /* the system dialog couldn't be turned off: never ours as well */
    frames(30);
    s_popup_off = 0; s_popup_works = 0;
    home();
    CHECK(!Q.open, "opened while the system dialog is on");
    puts(fails ? "quitdlg_preview: FAILED" : "quitdlg_preview: ok");
    return fails != 0;
}
