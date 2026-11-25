/* XLT - clock, battery, WLAN switch and CPU profiles. */
#include <psppower.h>
#include <psprtc.h>
#include <pspwlan.h>
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>
#include "system.h"
#include "bg.h"
#include "psreg.h"

static u64 s_last_tick;
static volatile int s_h12, s_date_fmt = 2, s_light_off, s_fmt_gen;
static int s_fmt_seen;
static int s_have_status;
static int s_profile;
static int s_wanted = 111; /* what idle/boost asked for last */
static volatile int s_hold;

int system_tick(SysStatus *st)
{
    u64 now;
    sceRtcGetCurrentTick(&now);
    int fmt = s_fmt_gen != s_fmt_seen;
    s_fmt_seen = s_fmt_gen;
    if (s_have_status && now - s_last_tick < 1000000ull)
        return fmt;
    s_last_tick = now;

    SysStatus n;
    ScePspDateTime t;
    sceRtcGetCurrentClockLocalTime(&t);
    n.hour = t.hour;
    n.minute = t.minute;
    n.battery = scePowerIsBatteryExist() ? scePowerGetBatteryLifePercent() : -1;
    n.charging = n.battery >= 0 && scePowerIsBatteryCharging();
    n.wifi_on = sceWlanGetSwitchState() == 1;
    n.day = t.day;
    n.month = t.month;
    n.wday = sceRtcGetDayOfWeek(t.year, t.month, t.day);
    int life = n.battery >= 0 && !n.charging ? scePowerGetBatteryLifeTime() : -1;
    n.life_min = life > 0 && life < 24 * 60 ? life : -1; /* the firmware gives nonsense right after a change */

    int changed = fmt || !s_have_status || n.hour != st->hour || n.minute != st->minute ||
                  n.battery != st->battery || n.charging != st->charging || n.wifi_on != st->wifi_on ||
                  n.day != st->day || n.life_min != st->life_min;
    *st = n;
    s_have_status = 1;
    return changed;
}


static void job_formats(void *arg)
{
    int v;
    if (psreg_get_int("/CONFIG/DATE", "time_format", &v) == 0)
        s_h12 = v == 1;
    if (psreg_get_int("/CONFIG/DATE", "date_format", &v) == 0 && v >= 0 && v <= 2)
        s_date_fmt = v;
    if (psreg_get_int("/CONFIG/SYSTEM/POWER_SAVING", "backlight_off_interval", &v) == 0 && v >= 0)
        s_light_off = v;
    s_fmt_gen++; /* system_tick redraws the clock in the new format */
}

void system_read_formats(void) { bg_post(job_formats, NULL, 0); }
int system_h12(void) { return s_h12; }
int system_date_fmt(void) { return s_date_fmt; }
int system_backlight_off(void) { return s_light_off; }

void format_clock(char *b, int n, int hour, int minute)
{
    if (!s_h12) {
        snprintf(b, n, "%02d:%02d", hour, minute);
        return;
    }
    int h = hour % 12;
    snprintf(b, n, "%d:%02d %s", h ? h : 12, minute, hour < 12 ? "AM" : "PM");
}

/* The clock, in two parts:
 *   - the profile (PLL, CPU and bus together, scePowerSetClockFrequency): 222/222/111
 *     for the interface, 333 only for the full-screen video player. A PLL change
 *     stalls the PSP for a whole frame (17 ms, measured on an E1004 2026-11-24): the
 *     old 111/222/333 switching made the screen jump and music click;
 *   - inside the 222 profile, the CPU alone (scePowerSetCpuClockFrequency, bus and
 *     PLL untouched): 111 when the interface has had nothing to draw for a second,
 *     222 as soon as it has, or while work runs (power_boost). Music plays at 111
 *     too: its player measures each block and asks for 222 (power_music_fast) when
 *     one takes over half its playing time. That change takes 11 us and costs no
 *     frame; nothing showed on screen or in music (clock test, 20 changes, same day). */
#define QUIET_FRAMES 60        /* a second without drawing before the CPU goes down */
#define STATS_FRAMES (5 * 60 * 60) /* a scan.log line every 5 minutes */

static int s_cpu = 222;        /* CPU clock inside the 222 profile */
static volatile int s_boost;   /* power_boost .. power_idle */
static volatile int s_music_fast; /* the music player wants 222 */
static int s_quiet;            /* frames without drawing */
static unsigned s_frames, s_low_frames, s_changes;
static void (*s_note)(const char *);

static void cpu_set(int mhz)
{
    if (s_cpu == mhz)
        return;
    if (scePowerSetCpuClockFrequency(mhz) >= 0) {
        s_cpu = mhz;
        s_changes++;
    }
}

static void set_profile(int mhz)
{
    if (s_profile == mhz) {
        if (mhz == 222)
            cpu_set(222); /* a hold or a restore wants the full 222 */
        return;
    }
    int r;
    switch (mhz) {
    case 111: r = scePowerSetClockFrequency(111, 111, 55); break;
    case 333: r = scePowerSetClockFrequency(333, 333, 166); break;
    default:  r = scePowerSetClockFrequency(222, 222, 111); break;
    }
    if (r >= 0) {
        s_profile = mhz;
        s_cpu = mhz;
    }
}

static void want(int mhz)
{
    s_wanted = mhz;
    if (!s_hold)
        set_profile(mhz);
}

void power_idle(void) { s_boost = 0; want(222); }
void power_boost(void) { s_boost = 1; want(222); }
void power_restore(void) { s_hold = 0; s_boost = 0; want(222); }

void power_hold(int mhz)
{
    s_hold = mhz;
    set_profile(mhz ? mhz : s_wanted);
}

void power_set_note(void (*fn)(const char *)) { s_note = fn; }
void power_music_fast(int on) { s_music_fast = on; }
int power_cpu_mhz(void) { return s_cpu; }

static void job_stats(void *arg)
{
    if (s_note) s_note((const char *)arg);
}

void power_frame(int busy)
{
    if (s_hold || s_profile != 222) {
        s_quiet = 0; /* the hold set the clock */
    } else if (busy || s_boost || s_music_fast) {
        s_quiet = 0;
        cpu_set(222);
    } else if (++s_quiet >= QUIET_FRAMES) {
        cpu_set(111);
    }
    s_frames++;
    if (s_cpu == 111 && s_profile == 222)
        s_low_frames++;
    if (s_frames >= STATS_FRAMES) {
        char b[120];
        snprintf(b, sizeof(b), "cpu: last 5 min %u%% at 111 MHz, %u changes (now cpu %d bus %d)",
                 s_low_frames * 100 / s_frames, s_changes, scePowerGetCpuClockFrequencyInt(),
                 scePowerGetBusClockFrequencyInt());
        bg_post(job_stats, b, (int)strlen(b) + 1);
        s_frames = s_low_frames = s_changes = 0;
    }
}

void format_date_short(char *b, int n, const SysStatus *st)
{
    static const char *const wd[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *const mo[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (st->month < 1 || st->month > 12 || st->wday < 0 || st->wday > 6) {
        if (n > 0) b[0] = 0;
        return;
    }
    if (system_date_fmt() == 1)
        snprintf(b, n, "%s %s %d", wd[st->wday], mo[st->month - 1], st->day);
    else
        snprintf(b, n, "%s %d %s", wd[st->wday], st->day, mo[st->month - 1]);
}

void format_life(char *b, int n, int minutes)
{
    if (minutes >= 60)
        snprintf(b, n, "%d h %d m", minutes / 60, minutes % 60);
    else
        snprintf(b, n, "%d m", minutes);
}
