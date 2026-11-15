/* XLT - the HOME dialog. See quitdlg.h. */
#include <stdio.h>
#include <string.h>
#include <pspctrl.h>
#include "quitdlg.h"
#include "anim.h"
#include "btntest.h"
#include "library.h"
#include "sfx.h"
#include "sysctl.h"
#include "ui_internal.h"

static struct {
    int open, yes;   /* yes: the cursor is on Yes */
    Spring t;
    int home_was;    /* HOME held last frame */
    int logged;
} Q;

void quitdlg_poll(Input *in)
{
    int r = sysctl_home_popup(0); /* once, then again only after the watchdog turned it on */
    sysctl_home_alive();
    if (!Q.logged) {
        Q.logged = 1;
        char msg[96];
        snprintf(msg, sizeof(msg), "HOME: XLT's own dialog %s (%d)",
                 r == -1 ? "unavailable: sceImposeSetHomePopup not found" : r < 0 ? "unavailable: the call failed" : "on", r);
        library_note(msg);
    }
    if (!sysctl_home_popup_off()) { /* the system's own dialog is the one that opens */
        Q.home_was = 0;
        return;
    }
    int home = (sysctl_kernel_buttons() & PSP_CTRL_HOME) != 0;
    if (home && !Q.home_was) in->pressed |= PSP_CTRL_HOME;
    if (home) in->held |= PSP_CTRL_HOME;
    Q.home_was = home;
}

static void close_dlg(void)
{
    Q.open = 0;
    spring_to(&Q.t, 0);
    sfx_play(SFX_CLOSE);
}

int quitdlg_update(Input *in)
{
    int old = Q.t.x;
    spring_step(&Q.t);
    int dirty = Q.t.x != old;
    unsigned p = in->pressed;
    in->pressed &= ~PSP_CTRL_HOME; /* nothing else uses it */
    in->held &= ~PSP_CTRL_HOME;
    if (!Q.open) {
        if (p & PSP_CTRL_HOME && !btntest_is_open()) {
            Q.open = 1;
            Q.yes = 1; /* HOME then ✕ quits, as with the system's dialog */
            spring_snap(&Q.t, 0);
            spring_to(&Q.t, FX(1));
            sfx_play(SFX_OPEN);
            in->pressed = 0;
            return 1;
        }
        return dirty;
    }
    if (p & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) {
        Q.yes = !Q.yes;
        sfx_play(SFX_MOVE);
    } else if (p & PSP_CTRL_CROSS) {
        if (Q.yes) {
            sfx_play(SFX_SELECT);
            Q.open = 0;
            ui_request_exit(); /* XLT's own exit: the same as the system's Yes */
        } else {
            close_dlg();
        }
    } else if (p & (PSP_CTRL_CIRCLE | PSP_CTRL_HOME)) {
        close_dlg();
    }
    in->pressed = 0;
    in->held = 0;
    return 1;
}

int quitdlg_is_open(void) { return Q.open || Q.t.x > 0; }

void quitdlg_release(void) { sysctl_home_popup(1); }

/* A Yes / No button; focused: the accent. */
static void button(int x, int y, int w, int h, const char *label, int focused)
{
    gfx_rrect(x, y, w, h, h / 2, focused ? C_ACCENT : C_RAISED);
    gfx_text_center(FONT_BODY, x + w / 2, y + (h - gfx_line_h(FONT_BODY)) / 2, label, focused ? C_WHITE : C_TEXT);
}

void quitdlg_draw(void)
{
    int a = Q.t.x;
    if (a <= 0) return;
    if (a > 256) a = 256;
    gfx_set_opacity(a);
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, WITH_ALPHA(0x000000, 150));
    const int w = 280, h = 140, x = (SCREEN_W - w) / 2;
    int y = (SCREEN_H - h) / 2 + (256 - a) * 12 / 256;
    gfx_rrect(x, y, w, h, 12, C_SURFACE);
    int fb = gfx_line_h(FONT_BOLD), fs = gfx_line_h(FONT_SMALL);
    gfx_text_center(FONT_BOLD, SCREEN_W / 2, y + 16, "Quit XLT?", C_TEXT);
    gfx_text_center(FONT_SMALL, SCREEN_W / 2, y + 16 + fb + 4, "You'll go back to the XMB.", C_MUTED);
    const int bw = 96, bh = 26, gap = 16, by = y + 16 + fb + 4 + fs + 16;
    int bx = SCREEN_W / 2 - bw - gap / 2;
    button(bx, by, bw, bh, "Yes", Q.yes);
    button(bx + bw + gap, by, bw, bh, "No", !Q.yes);
    static const Hint hints[] = {
        {ICON_BTN_CROSS, NULL, C_BTN_CROSS, "OK"},
        {ICON_BTN_CIRCLE, NULL, C_BTN_CIRCLE, "Cancel"},
    };
    ui_draw_hints(hints, 2, SCREEN_W / 2, y + h - fs - 8);
    gfx_set_opacity(256);
}
