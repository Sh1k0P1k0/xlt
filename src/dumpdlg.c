/* XLT - the "Copy to Memory Stick" dialog. See dumpdlg.h. */
#include <stdio.h>
#include <string.h>
#include <pspctrl.h>
#include "dumpdlg.h"
#include "anim.h"
#include "dumpname.h"
#include "library.h"
#include "sfx.h"
#include "umddump.h"
#include "ui_internal.h"

static struct {
    int open;
    Spring t;
    int asking;        /* "Stop copying?" is up */
    int stop_yes;      /* its cursor is on Stop */
    char title[128];   /* the game's */
    DumpStatus st;     /* as last drawn */
    int ended;         /* the end (done / failed / stopped) was seen */
} D;

void dumpdlg_open(const Entry *disc)
{
    const char *why = NULL;
    if (umddump_start(disc, &why) != 0) {
        ui_toast(why);
        sfx_play(SFX_ERROR);
        return;
    }
    memset(&D.st, 0, sizeof(D.st));
    snprintf(D.title, sizeof(D.title), "%s", disc->title);
    D.open = 1;
    D.asking = 0;
    D.ended = 0;
    spring_snap(&D.t, 0);
    spring_to(&D.t, FX(1));
    sfx_play(SFX_OPEN);
}

int dumpdlg_is_open(void) { return D.open || D.t.x > 0; }

static void close_dlg(void)
{
    D.open = 0;
    spring_to(&D.t, 0);
    sfx_play(SFX_CLOSE);
    umddump_clear();
}

int dumpdlg_update(Input *in)
{
    int old = D.t.x;
    spring_step(&D.t);
    int dirty = D.t.x != old;
    if (!D.open)
        return dirty;
    DumpStatus st;
    umddump_status(&st);
    /* Redrawn when the figures move by a visible step (or the state changes). */
    if (st.state != D.st.state || (st.done >> 20) != (D.st.done >> 20) || st.elapsed_ms / 1000 != D.st.elapsed_ms / 1000)
        dirty = 1;
    if (st.state != D.st.state && !D.ended && (st.state == DUMP_DONE || st.state == DUMP_FAILED || st.state == DUMP_STOPPED)) {
        D.ended = 1;
        D.asking = 0;
        if (st.state == DUMP_DONE) {
            sfx_play(SFX_SELECT);
            library_rescan(); /* the new ISO shows up in Games */
        } else if (st.state == DUMP_FAILED) {
            sfx_play(SFX_ERROR);
        } else {
            close_dlg(); /* stopped by the user: nothing to tell */
            D.st = st;
            in->pressed = in->held = 0;
            return 1;
        }
    }
    D.st = st;
    unsigned p = in->pressed;
    if (D.ended) {
        if (p & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE))
            close_dlg();
    } else if (D.asking) {
        if (p & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) {
            D.stop_yes = !D.stop_yes;
            sfx_play(SFX_MOVE);
            dirty = 1;
        } else if (p & PSP_CTRL_CROSS) {
            if (D.stop_yes) umddump_stop();
            D.asking = 0;
            sfx_play(D.stop_yes ? SFX_SELECT : SFX_CLOSE);
            dirty = 1;
        } else if (p & PSP_CTRL_CIRCLE) {
            D.asking = 0;
            sfx_play(SFX_CLOSE);
            dirty = 1;
        }
    } else if (p & PSP_CTRL_CIRCLE) {
        D.asking = 1;
        D.stop_yes = 0; /* ○ then ✕ keeps copying: stopping throws the copy away */
        sfx_play(SFX_OPEN);
        dirty = 1;
    }
    in->pressed = 0;
    in->held = 0;
    return dirty;
}

static void button(int x, int y, int w, int h, const char *label, int focused)
{
    gfx_rrect(x, y, w, h, h / 2, focused ? C_ACCENT : C_RAISED);
    gfx_text_center(FONT_BODY, x + w / 2, y + (h - gfx_line_h(FONT_BODY)) / 2, label, focused ? C_WHITE : C_TEXT);
}

/* Up to two lines of s centred in w (cut at a space). Returns the y after them. */
static int lines2(int font, int cx, int y, int w, const char *s, u32 c)
{
    int lh = gfx_line_h(font);
    if (gfx_text_width(font, s) <= w) {
        gfx_text_center(font, cx, y, s, c);
        return y + lh;
    }
    char a[160];
    int best = 0;
    for (int i = 1; s[i] && i < (int)sizeof(a) - 1; i++)
        if (s[i] == ' ') {
            snprintf(a, sizeof(a), "%.*s", i, s);
            if (gfx_text_width(font, a) > w) break;
            best = i;
        }
    if (!best) {
        gfx_text_fit(font, cx - w / 2, y, w, s, c);
        return y + lh;
    }
    snprintf(a, sizeof(a), "%.*s", best, s);
    gfx_text_center(font, cx, y, a, c);
    const char *rest = s + best + 1;
    if (gfx_text_width(font, rest) <= w) gfx_text_center(font, cx, y + lh, rest, c);
    else gfx_text_fit(font, cx - w / 2, y + lh, w, rest, c);
    return y + 2 * lh;
}

void dumpdlg_draw(void)
{
    int a = D.t.x;
    if (a <= 0) return;
    if (a > 256) a = 256;
    gfx_set_opacity(a);
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, WITH_ALPHA(0x000000, 150));
    const int w = 330, h = 168, x = (SCREEN_W - w) / 2, cx = SCREEN_W / 2, iw = w - 40;
    int y = (SCREEN_H - h) / 2 + (256 - a) * 12 / 256;
    gfx_rrect(x, y, w, h, 12, C_SURFACE);
    const DumpStatus *st = &D.st;
    int fb = gfx_line_h(FONT_BOLD), fs = gfx_line_h(FONT_SMALL);
    int ty = y + 14;
    const char *head = st->state == DUMP_DONE ? "Copied to the Memory Stick"
                     : st->state == DUMP_FAILED ? "The disc couldn't be copied"
                     : D.asking ? "Stop copying?" : "Copying the disc";
    gfx_text_center(FONT_BOLD, cx, ty, head, C_TEXT);
    ty += fb + 2;
    if (gfx_text_width(FONT_SMALL, D.title) <= iw) gfx_text_center(FONT_SMALL, cx, ty, D.title, C_MUTED);
    else gfx_text_fit(FONT_SMALL, x + 20, ty, iw, D.title, C_MUTED);
    ty += fs + 14;

    char l1[160], l2[96], a1[16], a2[16];
    const Hint *hints = NULL;
    int nh = 0;
    static const Hint copy_hints[] = {{ICON_BTN_CIRCLE, NULL, C_BTN_CIRCLE, "Stop"}};
    static const Hint ask_hints[] = {
        {ICON_BTN_CROSS, NULL, C_BTN_CROSS, "OK"},
        {ICON_BTN_CIRCLE, NULL, C_BTN_CIRCLE, "Back"},
    };
    static const Hint end_hints[] = {{ICON_BTN_CROSS, NULL, C_BTN_CROSS, "OK"}};

    if (D.asking) {
        lines2(FONT_SMALL, cx, ty, iw, "What was copied so far is deleted.", C_MUTED);
        const int bw = 120, bh = 26, gap = 14, by = ty + fs + 10;
        button(cx - bw - gap / 2, by, bw, bh, "Stop", D.stop_yes);
        button(cx + gap / 2, by, bw, bh, "Keep copying", !D.stop_yes);
        hints = ask_hints; nh = 2;
    } else if (st->state == DUMP_PREPARING || st->state == DUMP_IDLE) {
        if (st->pause_s > 0) { /* the driver's count: the PSP stops on this frame */
            char l[64];
            snprintf(l, sizeof(l), "The PSP pauses for about %d seconds.", st->pause_s);
            gfx_text_center(FONT_SMALL, cx, ty, "Checking the free space...", C_MUTED);
            gfx_text_center(FONT_SMALL, cx, ty + fs + 2, l, C_MUTED);
        } else {
            gfx_text_center(FONT_SMALL, cx, ty + 8, "Checking the disc and the free space...", C_MUTED);
        }
        hints = copy_hints; nh = 1;
    } else if (st->state == DUMP_COPYING) {
        int pct = st->total ? (int)(st->done * 1000 / st->total) : 0;
        gfx_rrect(x + 20, ty, iw, 6, 3, C_RAISED);
        if (pct > 0) gfx_rrect(x + 20, ty, iw * pct / 1000 < 6 ? 6 : iw * pct / 1000, 6, 3, C_FOCUS);
        ty += 14;
        format_size(a1, sizeof(a1), st->done);
        format_size(a2, sizeof(a2), st->total);
        snprintf(l1, sizeof(l1), "%s of %s", a1, a2);
        gfx_text(FONT_SMALL, x + 20, ty, l1, C_TEXT);
        if (st->elapsed_ms >= 2000) {
            unsigned kbs = (unsigned)(st->done / st->elapsed_ms); /* bytes/ms = KB/s near enough */
            snprintf(l2, sizeof(l2), "%u.%u MB/s", kbs / 1000, kbs % 1000 / 100);
            gfx_text(FONT_SMALL, x + 20 + iw - gfx_text_width(FONT_SMALL, l2), ty, l2, C_MUTED);
        }
        ty += fs + 2;
        dump_time_left(st->done, st->total, st->elapsed_ms, l2, sizeof(l2));
        gfx_text_center(FONT_SMALL, cx, ty, l2[0] ? l2 : "Working out the time left...", C_MUTED);
        hints = copy_hints; nh = 1;
    } else if (st->state == DUMP_DONE) {
        snprintf(l1, sizeof(l1), "ISO/%s", st->name);
        ty = lines2(FONT_SMALL, cx, ty, iw, l1, C_TEXT);
        format_size(a1, sizeof(a1), st->total);
        unsigned s = st->elapsed_ms / 1000;
        if (s >= 60) snprintf(l2, sizeof(l2), "%s in %u min %u s. It's in Games now.", a1, s / 60, s % 60);
        else snprintf(l2, sizeof(l2), "%s in %u s. It's in Games now.", a1, s);
        gfx_text_center(FONT_SMALL, cx, ty + 4, l2, C_MUTED);
        hints = end_hints; nh = 1;
    } else if (st->state == DUMP_FAILED) {
        lines2(FONT_SMALL, cx, ty, iw, st->error[0] ? st->error : "Something went wrong.", C_TEXT);
        gfx_text_center(FONT_SMALL, cx, ty + 2 * fs + 6, "Nothing was left on the Memory Stick.", C_MUTED);
        hints = end_hints; nh = 1;
    }
    if (nh)
        ui_draw_hints(hints, nh, cx, y + h - fs - 10);
    gfx_set_opacity(256);
}
