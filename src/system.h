#ifndef SYSTEM_H
#define SYSTEM_H

typedef struct {
    int hour, minute;
    int battery;  /* percent, -1 if no battery */
    int charging;
    int wifi_on;  /* WLAN switch */
    int day, month, wday; /* the date: 1..31, 1..12, 0 = Sunday (0 month: not known) */
    int life_min; /* battery time left in minutes, -1 if charging, no battery or not known */
} SysStatus;

/* "Sun 22 Nov" ("Sun Nov 22" when the PSP's date format puts the month first). */
void format_date_short(char *b, int n, const SysStatus *st);
/* "3 h 10 m", or "45 m". */
void format_life(char *b, int n, int minutes);

/* Refreshes the status at most once per second. Returns 1 if anything shown changed. */
int system_tick(SysStatus *st);

/* The PSP's own time and date formats (System settings, in the registry).
 * system_read_formats() reads them on the background worker; until they
 * arrive the defaults are 24-hour and DD/MM/YYYY. */
void system_read_formats(void);
int system_h12(void);          /* 1: 12-hour clock */
int system_date_fmt(void);     /* registry date_format: 0 YYYY/MM/DD, 1 MM/DD/YYYY, 2 DD/MM/YYYY */
int system_backlight_off(void); /* firmware backlight auto-off in seconds, 0 = never */
/* "21:41", or "9:41 PM" with the 12-hour clock. */
void format_clock(char *b, int n, int hour, int minute);

/* The clock (system.c): the 222 MHz profile for the interface, its CPU alone down to
 * 111 when nothing has been drawn for a second. boost: work running (a scan, a
 * decode, a copy) keeps the CPU at 222 until idle. */
void power_idle(void);
void power_boost(void);
void power_restore(void); /* 222 MHz, the firmware default, on exit */

/* Holds the clock at mhz (0 = release) and ignores idle/boost until released.
 * Used by the full-screen video player (333 MHz). */
void power_hold(int mhz);

/* From the music player thread: 1 = its blocks are taking too long at 111 MHz, keep
 * the CPU at 222 (like boost); 0 = 111 is fine again. Music on its own holds nothing. */
void power_music_fast(int on);
/* The CPU clock the governor last set (111 or 222 inside the interface profile). */
int power_cpu_mhz(void);

/* Once per frame from the main loop, right after the vblank: busy = something will be
 * drawn this frame (the screensaver's redraws don't count). */
void power_frame(int busy);
/* Where the 5-minute summary goes (scan.log), called on the worker. */
void power_set_note(void (*fn)(const char *));

#endif
