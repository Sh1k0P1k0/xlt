/* XLT - the button tester. See btntest.h.
 *
 * The pad is read here directly (sceCtrlPeekBufferPositive for the buttons and
 * the stick, sysctl_kernel_buttons for HOME, NOTE, SCREEN and the volume keys,
 * which user mode doesn't see), not through input.c, which turns the stick into
 * a d-pad. Buttons still held from the press that opened it only count once
 * they have been let go. The system's HOME dialog is off while XLT runs, and
 * XLT's own (quitdlg.c) stays shut while the tester is up, so HOME is just
 * another button here. */
#include <stdio.h>
#include <string.h>
#include <pspctrl.h>
#include "btntest.h"
#include "anim.h"
#include "sfx.h"
#include "sysctl.h"
#include "ui_internal.h"

#define LEAVE_FRAMES 90 /* ○ held 1.5 s */

enum {
    B_UP, B_DOWN, B_LEFT, B_RIGHT, B_TRIANGLE, B_CIRCLE, B_CROSS, B_SQUARE, B_L, B_R,
    B_SELECT, B_START, B_HOME, B_VOLDOWN, B_VOLUP, B_NOTE, B_SCREEN, B_COUNT
};
static const unsigned MASK[B_COUNT] = {
    PSP_CTRL_UP, PSP_CTRL_DOWN, PSP_CTRL_LEFT, PSP_CTRL_RIGHT, PSP_CTRL_TRIANGLE, PSP_CTRL_CIRCLE,
    PSP_CTRL_CROSS, PSP_CTRL_SQUARE, PSP_CTRL_LTRIGGER, PSP_CTRL_RTRIGGER, PSP_CTRL_SELECT, PSP_CTRL_START,
    PSP_CTRL_HOME, PSP_CTRL_VOLDOWN, PSP_CTRL_VOLUP, PSP_CTRL_NOTE, PSP_CTRL_SCREEN,
};
#define KERNEL_ONLY (PSP_CTRL_HOME | PSP_CTRL_NOTE | PSP_CTRL_SCREEN | PSP_CTRL_VOLUP | PSP_CTRL_VOLDOWN | \
                     PSP_CTRL_HOLD | PSP_CTRL_WLAN_UP | PSP_CTRL_REMOTE | PSP_CTRL_DISC)

static struct {
    int open;
    Spring t;
    unsigned held, seen, stale;
    int lx, ly, minx, maxx, miny, maxy;
    int leave;   /* frames ○ has been held */
    int model;
} B;

void btntest_open(void)
{
    memset(&B, 0, sizeof(B));
    B.open = 1;
    B.lx = B.ly = 128;
    B.minx = B.miny = 255;
    B.stale = ~0u; /* whatever is held now (the ✕ that opened it) waits to be let go */
    B.model = sysctl_model();
    spring_snap(&B.t, 0);
    spring_to(&B.t, FX(1));
    sfx_play(SFX_OPEN);
}

int btntest_is_open(void) { return B.open || B.t.x > 0; }
int btntest_covers(void) { return B.t.x >= FX(1) && B.open; }

static unsigned counted(void)
{
    unsigned m = 0;
    for (int i = 0; i < B_COUNT; i++) m |= MASK[i];
    return m;
}

int btntest_update(Input *in)
{
    if (!btntest_is_open()) return 0;
    int moved = spring_step(&B.t);
    if (!B.open) return moved || B.t.x > 0;
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(&pad, 1);
    unsigned k = sysctl_kernel_buttons();
    unsigned held = (pad.Buttons & ~KERNEL_ONLY) | (k & KERNEL_ONLY);
    if (!k) held |= pad.Buttons & KERNEL_ONLY; /* no kernel read: what user mode has */
    B.stale &= held;
    unsigned fresh = held & ~B.stale & counted();
    if (fresh & ~B.seen & ~B.held) sfx_play(SFX_KEY);
    B.seen |= fresh;
    B.held = held;
    B.lx = pad.Lx;
    B.ly = pad.Ly;
    if (B.lx < B.minx) B.minx = B.lx;
    if (B.lx > B.maxx) B.maxx = B.lx;
    if (B.ly < B.miny) B.miny = B.ly;
    if (B.ly > B.maxy) B.maxy = B.ly;
    if (held & PSP_CTRL_CIRCLE & ~B.stale) {
        if (++B.leave >= LEAVE_FRAMES) {
            B.open = 0;
            spring_to(&B.t, 0);
            sfx_play(SFX_CLOSE);
        }
    } else {
        B.leave = 0;
    }
    in->pressed = 0;
    in->held = 0;
    return 1;
}

/* ------------------------------------------------------------------- draw */

static int is_down(int b) { return (B.held & MASK[b]) != 0; }
static int was_seen(int b) { return (B.seen & MASK[b]) != 0; }

/* A key: accent while held, its outline accented once it has been pressed. */
static void key(int b, int x, int y, int w, int h, int r, const char *label, int icon, u32 tint)
{
    int down = is_down(b), seen = was_seen(b);
    gfx_rrect(x, y, w, h, r, down ? C_ACCENT : seen ? C_ACCENT_DK : C_RAISED);
    u32 c = down ? C_WHITE : seen ? C_TEXT : C_MUTED;
    if (icon >= 0) {
        int s = 14;
        gfx_icon(icon, x + (w - s) / 2, y + (h - s) / 2, s, s, down ? C_WHITE : seen ? tint : C_MUTED);
    } else if (label) {
        int fs = gfx_line_h(FONT_SMALL);
        gfx_text_center(FONT_SMALL, x + w / 2, y + (h - fs) / 2, label, c);
    }
}

/* A line centred on the screen area, cut with an ellipsis if it is still too wide. */
static void line(int font, int y, int w, const char *s, u32 c)
{
    if (gfx_text_width(font, s) <= w) gfx_text_center(font, SCREEN_W / 2, y, s, c);
    else gfx_text_fit(font, (SCREEN_W - w) / 2, y, w, s, c);
}

static void state_pill(int *x, int y, const char *name, const char *val, int on)
{
    char s[48];
    snprintf(s, sizeof(s), "%s: %s", name, val);
    int w = gfx_text_width(FONT_SMALL, s) + 16, fs = gfx_line_h(FONT_SMALL);
    gfx_rrect(*x, y, w, fs + 6, (fs + 6) / 2, on ? C_ACCENT_DK : C_RAISED);
    gfx_text(FONT_SMALL, *x + 8, y + 3, s, on ? C_WHITE : C_MUTED);
    *x += w + 6;
}

void btntest_draw(const SysStatus *st)
{
    int t = B.t.x;
    if (t <= 0) return;
    if (t > 256) t = 256;
    gfx_set_opacity(t);
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BG);
    ui_draw_status_bar("Button test", st);
    int fs = gfx_line_h(FONT_SMALL);

    /* the shoulders, then the body over them */
    key(B_L, 52, 24, 100, 20, 8, "L", -1, 0);
    key(B_R, SCREEN_W - 152, 24, 100, 20, 8, "R", -1, 0);
    const int bx = 30, by = 47, bw = SCREEN_W - 60, bh = 161;
    gfx_rrect(bx, by, bw, bh, 46, C_SURFACE);

    /* d-pad */
    const int dx = 96, dy = 88;
    key(B_UP, dx - 10, dy - 31, 20, 22, 5, NULL, -1, 0);
    key(B_DOWN, dx - 10, dy + 9, 20, 22, 5, NULL, -1, 0);
    key(B_LEFT, dx - 31, dy - 10, 22, 20, 5, NULL, -1, 0);
    key(B_RIGHT, dx + 9, dy - 10, 22, 20, 5, NULL, -1, 0);
    gfx_text_center(FONT_SMALL, dx, dy - 20 - fs / 2 + 1, "^", is_down(B_UP) ? C_WHITE : C_MUTED);
    gfx_text_center(FONT_SMALL, dx, dy + 20 - fs / 2, "v", is_down(B_DOWN) ? C_WHITE : C_MUTED);
    gfx_text_center(FONT_SMALL, dx - 20, dy - fs / 2, "<", is_down(B_LEFT) ? C_WHITE : C_MUTED);
    gfx_text_center(FONT_SMALL, dx + 20, dy - fs / 2, ">", is_down(B_RIGHT) ? C_WHITE : C_MUTED);

    /* the stick: its range so far (a box), where it is (the dot) */
    const int sx = 96, sy = 148, sr = 22;
    gfx_rrect(sx - sr, sy - sr, sr * 2, sr * 2, sr, C_RAISED);
    if (B.maxx >= B.minx) {
        int x0 = sx - sr + B.minx * sr * 2 / 256, x1 = sx - sr + (B.maxx + 1) * sr * 2 / 256;
        int y0 = sy - sr + B.miny * sr * 2 / 256, y1 = sy - sr + (B.maxy + 1) * sr * 2 / 256;
        gfx_rect(x0, y0, x1 - x0, 1, C_LINE);
        gfx_rect(x0, y1 - 1, x1 - x0, 1, C_LINE);
        gfx_rect(x0, y0, 1, y1 - y0, C_LINE);
        gfx_rect(x1 - 1, y0, 1, y1 - y0, C_LINE);
    }
    gfx_rect(sx - sr + 4, sy, sr * 2 - 8, 1, C_LINE);
    gfx_rect(sx, sy - sr + 4, 1, sr * 2 - 8, C_LINE);
    int off = (B.lx < 40 || B.lx > 215 || B.ly < 40 || B.ly > 215); /* where XLT reads it as a d-pad press */
    int px = sx - sr + B.lx * sr * 2 / 256, py = sy - sr + B.ly * sr * 2 / 256;
    gfx_rrect(px - 7, py - 7, 14, 14, 7, off ? C_ACCENT : C_TEXT);

    /* face buttons */
    const int fx = SCREEN_W - 96, fy = 88;
    key(B_TRIANGLE, fx - 12, fy - 36, 24, 24, 12, NULL, ICON_BTN_TRIANGLE, C_BTN_TRIANGLE);
    key(B_CIRCLE, fx + 12, fy - 12, 24, 24, 12, NULL, ICON_BTN_CIRCLE, C_BTN_CIRCLE);
    key(B_CROSS, fx - 12, fy + 12, 24, 24, 12, NULL, ICON_BTN_CROSS, C_BTN_CROSS);
    key(B_SQUARE, fx - 36, fy - 12, 24, 24, 12, NULL, ICON_BTN_SQUARE, C_BTN_SQUARE);

    /* the screen: numbers (between the d-pad and the face buttons) */
    const int cx = 134, cy = 54, cw = SCREEN_W - 268, ch = 118, tw = cw - 12; /* ends above the bottom row */
    gfx_rrect(cx, cy, cw, ch, 6, C_BG);
    int n = 0;
    for (int i = 0; i < B_COUNT; i++) n += was_seen(i);
    char s[64];
    snprintf(s, sizeof(s), "%d of %d pressed", n, B_COUNT);
    line(FONT_BODY, cy + 8, tw, s, n == B_COUNT ? C_ACCENT : C_TEXT);
    int ly = cy + 8 + gfx_line_h(FONT_BODY) + 8;
    snprintf(s, sizeof(s), "Stick X %d  Y %d", B.lx, B.ly);
    line(FONT_SMALL, ly, tw, s, C_TEXT);
    if (B.maxx >= B.minx) {
        snprintf(s, sizeof(s), "Range X %d-%d  Y %d-%d", B.minx, B.maxx, B.miny, B.maxy);
        line(FONT_SMALL, ly + fs + 2, tw, s, C_MUTED);
    }
    line(FONT_SMALL, ly + 2 * (fs + 2), tw, "At rest it should read", C_MUTED);
    line(FONT_SMALL, ly + 3 * (fs + 2), tw, "near 128; far off: drift.", C_MUTED);

    /* the row under the screen */
    static const struct { int b; const char *label; } ROW[] = {
        {B_HOME, "HOME"}, {B_VOLDOWN, "VOL -"}, {B_VOLUP, "VOL +"}, {B_SCREEN, "SCREEN"},
        {B_NOTE, "NOTE"}, {B_SELECT, "SELECT"}, {B_START, "START"},
    };
    const int rg = 4, rn = COUNT(ROW);
    int rws[COUNT(ROW)], row_w = (rn - 1) * rg;
    for (int i = 0; i < rn; i++) row_w += rws[i] = gfx_text_width(FONT_SMALL, ROW[i].label) + 14;
    int row_x = (SCREEN_W - row_w) / 2;
    for (int i = 0; i < rn; i++) { /* under the screen, clear of the stick */
        key(ROW[i].b, row_x, by + bh - 30, rws[i], 20, 10, ROW[i].label, -1, 0);
        row_x += rws[i] + rg;
    }

    /* the switches: shown, not counted */
    int px2, y2 = by + bh + 8;
    int hold = (B.held & PSP_CTRL_HOLD) != 0, wlan = (B.held & PSP_CTRL_WLAN_UP) != 0;
    int remote = (B.held & PSP_CTRL_REMOTE) != 0, disc = (B.held & PSP_CTRL_DISC) != 0;
    /* measure first, to centre the row */
    const char *names[4], *vals[4];
    int ons[4], k = 0;
    names[k] = "HOLD"; vals[k] = hold ? "on" : "off"; ons[k++] = hold;
    if (B.model != 10) { names[k] = "Wi-Fi switch"; vals[k] = wlan ? "on" : "off"; ons[k++] = wlan; }
    names[k] = "Headphones"; vals[k] = remote ? "in" : "out"; ons[k++] = remote;
    if (B.model != 4) { names[k] = "UMD"; vals[k] = disc ? "in" : "none"; ons[k++] = disc; }
    int total = 0;
    for (int i = 0; i < k; i++) {
        snprintf(s, sizeof(s), "%s: %s", names[i], vals[i]);
        total += gfx_text_width(FONT_SMALL, s) + 16 + (i ? 6 : 0);
    }
    px2 = (SCREEN_W - total) / 2;
    for (int i = 0; i < k; i++) state_pill(&px2, y2, names[i], vals[i], ons[i]);

    /* footer: how to leave, and how far ○ has got */
    if (!NUMB) gfx_rect(0, 248, SCREEN_W, 1, C_LINE); /* nuMB's hint row has no line (N5) */
    static const Hint leave[] = {{ICON_BTN_CIRCLE, NULL, C_BTN_CIRCLE, "Hold to leave"}};
    ui_draw_screen_footer(leave, 1, 253, 0); /* (every button is being tested: no quick menu) */
    if (B.leave > 0) {
        int w = 120 * B.leave / LEAVE_FRAMES;
        gfx_rrect(SCREEN_W / 2 - 60, 268, 120, 3, 1, C_RAISED);
        gfx_rrect(SCREEN_W / 2 - 60, 268, w < 3 ? 3 : w, 3, 1, C_ACCENT);
    }
    gfx_set_opacity(256);
}
