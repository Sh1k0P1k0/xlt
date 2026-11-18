/* XLT - screensaver controller.
 *
 * Idle time is counted in frames (the main loop runs once per vblank). It
 * doesn't start during full-screen video, and a video's running time doesn't
 * count. While it shows:
 *   - it redraws 30 times a second (the lights drift, the colon breathes) and
 *     everything underneath is skipped; previews pause (ui.c);
 *   - once it has faded in, the backlight eases down to half the user's level,
 *     and comes back at once on wake; not when the firmware's own backlight
 *     auto-off is due by then (setting the level could light the panel again);
 *     after a wake the level is held for a few seconds, since the firmware can
 *     put the dimmed level back when it relights the panel or wakes from sleep,
 *     and the next dim starts from the remembered level, never a dimmed one;
 *   - the firmware dims the panel itself after about a minute idle (level 32)
 *     and turns it off at the backlight auto-off: XLT doesn't dim a panel the
 *     firmware has already dimmed, and when the firmware lights it again after
 *     auto-off (it keeps that press) the screensaver wakes as if pressed;
 *   - the PSP's own date and time formats are used (read from the registry on
 *     the background worker each time it starts).
 * The press that wakes it is eaten, and so is that button's auto-repeat until
 * every button is released.
 */
#include <pspkernel.h>
#include <psprtc.h>
#include <stdio.h>
#include "saver.h"
#include "saverdraw.h"
#include "ui_internal.h"
#include "gamecfg.h"
#include "music.h"
#include "viewer.h"
#include "sysctl.h"
#include "video.h"
#include "cc.h"

#define BUTTONS (PSP_CTRL_SELECT | PSP_CTRL_START | PSP_CTRL_UP | PSP_CTRL_RIGHT | PSP_CTRL_DOWN | PSP_CTRL_LEFT | \
                 PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_TRIANGLE | PSP_CTRL_CIRCLE | PSP_CTRL_CROSS |     \
                 PSP_CTRL_SQUARE)
#define FADE_IN 48   /* frames */
#define FADE_OUT 16
#define HINT_FRAMES (4 * 60)
#define DIM_STEPS 6  /* the backlight goes down in 6 steps... */
#define DIM_EVERY 10 /* ...10 frames apart */

enum { OFF, IN, ON, OUT };

static struct {
    int phase;
    int t;              /* frames in this phase */
    unsigned idle;      /* frames without a press */
    int eat;            /* swallow presses until every button is up */
    unsigned frame;     /* animation clock */
    int last_min;
    int wday, day, month, year;
    int dim_from;       /* backlight % before dimming, -1 = not dimmed */
    int dim_step;
    int dim_hw;         /* panel level the last dim step set, -1 = none */
    int orig;           /* the level before that dim (for g_cfg.brightness == -1) */
    int hold_pct;       /* level to keep after a wake... */
    int hold;           /* ...for this many more frames */
    int shown;          /* frames fully shown, for the hint */
} S = {.dim_from = -1, .last_min = -1, .dim_hw = -1, .orig = -1};

#define HOLD_FRAMES (4 * 60)

static volatile int s_resumed;

void saver_init(void)
{
    saverdraw_init();
}

void saver_on_resume(void) { s_resumed = 1; }
int saver_is_open(void) { return S.phase != OFF; }
int saver_covers(void) { return S.phase == ON; }

void saver_start(void)
{
    if (S.phase == ON || S.phase == IN)
        return;
    system_read_formats(); /* changed in the XMB or Settings? arrives within the fade */
    u64 tick;
    sceRtcGetCurrentTick(&tick);
    S.frame = (unsigned)(tick / 16667); /* the lights pick up where the wall clock puts them */
    S.last_min = -1;
    S.phase = IN;
    S.t = 0;
    S.dim_step = 0;
    S.shown = 0;
}

/* Back to the user's level: from XLT's dim, and also from the firmware's own
 * (it dims the panel after a minute idle and lights it dim after auto-off). */
static void undim(void)
{
    int pct = g_cfg.brightness >= 0 ? g_cfg.brightness : S.dim_from;
    if (pct < 0)
        return;
    S.hold_pct = pct;
    S.hold = HOLD_FRAMES;
    sysctl_brightness_set(S.hold_pct);
    S.dim_from = -1;
}

/* After a wake: put the level back if something (the firmware relighting the
 * panel, or coming back from sleep) changed it. Not while the control center
 * is open, where the user may be moving the slider. */
static void hold_level(void)
{
    if (S.hold <= 0)
        return;
    if (cc_is_open()) {
        S.hold = 0;
        return;
    }
    if (--S.hold % 15 == 0) {
        int now = sysctl_brightness_hw();
        if (now >= 0 && now != brightness_to_hw(S.hold_pct))
            sysctl_brightness_set(S.hold_pct);
    }
}

/* Returns 1 when the firmware has just lit the panel again after its backlight
 * auto-off (level 0 -> its dim level). The press that did it never reaches XLT. */
static int watch_level(void)
{
    static int last_hw = -2, tick;
    if (++tick % 6)
        return 0;
    int hw = sysctl_brightness_hw();
    if (hw == last_hw)
        return 0;
    int was = last_hw;
    last_hw = hw;
    return was == 0 && hw > 0 && hw != sysctl_brightness_last_set();
}

/* The level a new dim starts from. A level still at what the last dim set
 * (the restore didn't take) is not the user's: use the one from before it. */
static int level_before_dim(void)
{
    if (g_cfg.brightness >= 0)
        return g_cfg.brightness;
    int hw = sysctl_brightness_hw();
    if (S.dim_hw >= 0 && hw == S.dim_hw && S.orig >= 0)
        return S.orig;
    return sysctl_brightness_get();
}

static void wake(void)
{
    undim();
    S.phase = OUT;
    S.t = 0;
}

int saver_update(Input *in)
{
    unsigned pressed = in->pressed & BUTTONS;
    if (S.eat) {
        if (!(in->held & BUTTONS))
            S.eat = 0;
        in->pressed = 0;
        pressed = 0;
    }
    if (pressed || video_is_open() || viewer_slideshow())
        S.idle = 0;
    else if (S.idle < 0xFFFFFFF0u)
        S.idle++;

    if (s_resumed) { /* back from sleep: show what was open */
        s_resumed = 0;
        S.idle = 0;
        if (S.phase == IN || S.phase == ON)
            wake(); /* restores and holds the level; the firmware's own comes back for a moment */
    }
    hold_level();
    if (watch_level()) { /* a press woke the panel; the firmware kept it */
        if (S.phase == IN || S.phase == ON) {
            S.eat = 1;
            S.idle = 0;
            wake();
            return 1;
        }
        if (g_cfg.brightness >= 0 && !cc_is_open())
            sysctl_brightness_set(g_cfg.brightness);
    }

    /* A press outside the screensaver with the panel not at the user's level
     * (the firmware's own dimming, or a restore that didn't take): put it back. */
    if (pressed && S.phase == OFF && S.hold <= 0 && g_cfg.brightness >= 0 && !cc_is_open() &&
        sysctl_brightness_available()) {
        int now = sysctl_brightness_hw();
        if (now >= 0 && now != brightness_to_hw(g_cfg.brightness))
            sysctl_brightness_set(g_cfg.brightness);
    }

    switch (S.phase) {
    case OFF:
        if (g_cfg.saver_min > 0 && S.idle >= (unsigned)g_cfg.saver_min * 3600u)
            saver_start();
        if (S.phase == OFF)
            return 0;
        break;
    case IN:
    case ON:
        if (pressed) {
            in->pressed = 0;
            S.eat = 1;
            wake();
            return 1;
        }
        in->pressed = 0;
        break;
    case OUT:
        break;
    }

    S.frame++;
    S.t++;
    if (S.phase == ON)
        S.shown++;
    if (S.phase == IN && S.t >= FADE_IN) {
        S.phase = ON;
        S.t = 0;
    } else if (S.phase == OUT && S.t >= FADE_OUT) {
        S.phase = OFF;
        S.idle = 0;
        return 1;
    }

    /* Ease the backlight down once it is fully up. */
    if (S.phase == ON && S.dim_step < DIM_STEPS && S.t % DIM_EVERY == 0 && sysctl_brightness_available()) {
        if (S.dim_step == 0) {
            int dark = system_backlight_off() > 0 && S.idle + 2 * 60 >= (unsigned)system_backlight_off() * 60u;
            S.dim_from = dark ? -1 : level_before_dim();
            S.hold = 0;
            /* Only from the user's level: the firmware may have dimmed it lower already
             * (then XLT's "dim" would make it brighter), or turned it off. */
            int now = sysctl_brightness_hw();
            int theirs = S.dim_from >= 0 && now != brightness_to_hw(S.dim_from);
            if (theirs) {
                S.dim_from = -1;
                S.dim_step = DIM_STEPS;
            }
        } else if (S.dim_hw >= 0 && sysctl_brightness_hw() != S.dim_hw) { /* the firmware changed it */
            S.dim_step = DIM_STEPS; /* dim_from stays: waking restores the user's level */
        }
        if (S.dim_step >= DIM_STEPS) {
            /* stopped above */
        } else if (S.dim_from > 0) {
            S.dim_step++;
            int pct = S.dim_from - S.dim_from / 2 * S.dim_step / DIM_STEPS;
            sysctl_brightness_set(pct);
            S.orig = S.dim_from;
            S.dim_hw = brightness_to_hw(pct);
        } else {
            S.dim_from = -1;
            S.dim_step = DIM_STEPS; /* at the lowest level already, unknown, or the panel goes dark anyway */
        }
    }
    return S.phase != ON || (S.frame & 1) == 0;
}

/* smoothstep, 0..256 */
static int ease(int t, int n)
{
    if (t <= 0) return 0;
    if (t >= n) return 256;
    int x = (t << 8) / n;
    return (x * x * (768 - 2 * x)) >> 16;
}

void saver_draw(const SysStatus *st)
{
    if (S.phase == OFF)
        return;
    if (st->minute != S.last_min) { /* the date only changes on a new minute */
        ScePspDateTime t;
        sceRtcGetCurrentClockLocalTime(&t);
        S.year = t.year;
        S.month = t.month;
        S.day = t.day;
        S.wday = sceRtcGetDayOfWeek(t.year, t.month, t.day);
        S.last_min = st->minute;
    }

    SaverView v = {0};
    v.st = *st;
    v.wday = S.wday;
    v.day = S.day;
    v.month = S.month;
    v.year = S.year;
    v.h12 = system_h12();
    v.date_fmt = system_date_fmt();
    v.frame = S.frame;
    v.show = S.phase == IN ? ease(S.t, FADE_IN) : S.phase == OUT ? 256 - ease(S.t, FADE_OUT) : 256;
    v.leaving = S.phase == OUT;
    /* "Press any button" for a few seconds after it appears */
    v.hint = S.phase == IN ? v.show : 256 - ease(S.shown - HINT_FRAMES, 60);

    static char music[160];
    MusicNow now;
    if (music_state() != MUSIC_STOPPED && music_now(&now) == 0) {
        if (now.artist && now.artist[0])
            snprintf(music, sizeof(music), "%s" DOT "%s", now.title, now.artist);
        else
            snprintf(music, sizeof(music), "%s", now.title);
        v.music = music;
        v.music_paused = music_state() == MUSIC_PAUSED;
    }
    saverdraw(&v);
}
