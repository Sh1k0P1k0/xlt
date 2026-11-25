/* XLT - shared between the screens (ui.c) and the sheets (opts.c, cc.c). */
#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include <pspctrl.h>
#include "gfx.h"
#include "entry.h"
#include "system.h"

#include "theme.h"
#include "gamecfg.h"

/* nuMB (docs/numb.md): Settings > XLT > Interface. Only drawing and navigation follow it. */
#define NUMB (g_cfg.ui_mode == UI_NUMB)

/* nuMB's cross bar and column (ui.c; Settings draws its own column the same way, N4) */
#define NUMB_FOCUS_X 100 /* the focused category's centre on the bar */
#define NUMB_BAR_CY 56
#define NUMB_PITCH 58
#define NUMB_BOX 50
#define NUMB_ICON 26
#define NUMB_LABEL_Y 86
#define NUMB_COL_X (NUMB_FOCUS_X - NUMB_BOX / 2)
#define NUMB_COL_W (SCREEN_W - 12 - NUMB_COL_X)
#define NUMB_COL_Y 108
#define NUMB_COL_BOTTOM 244
#define NUMB_ROW_H 26

/* The palette: from the theme in use (theme.c). */
#define C_BG      (g_th->bg)
#define C_SURFACE (g_th->surface)
#define C_RAISED  (g_th->raised)
#define C_LINE    (g_th->line)
#define C_TEXT    (g_th->text)
#define C_MUTED   (g_th->muted)
#define C_ACCENT  (g_th->accent)
#define C_FOCUS   (g_th->focus)
#define C_PILL    (g_th->pill)
#define C_MARK    (g_th->mark)
#define C_ACCENT_DK (g_th->accent_dk)
#define C_WHITE   RGB(0xffffff)
#define C_LOW     RGB(0xe5534b)
#define C_CHARGE  RGB(0x57ab5a)

#define C_BTN_CROSS    RGB(0x8fb3ee)
#define C_BTN_CIRCLE   RGB(0xec8e8e)
#define C_BTN_TRIANGLE RGB(0x7fd3ad)
#define C_BTN_SQUARE   RGB(0xe0a3e0)
/* The button tints above are pastel for the dark themes; on the light theme
 * the same colour comes out deeper (ui_draw_hints does it for every hint). */
static inline u32 btn_tint(u32 c)
{
    if (!g_th->light) return c;
    return c == C_BTN_CROSS ? RGB(0x2f6fd0) : c == C_BTN_CIRCLE ? RGB(0xc9473f)
         : c == C_BTN_TRIANGLE ? RGB(0x1f8a5b) : c == C_BTN_SQUARE ? RGB(0xa845a8) : c;
}

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define DOT " \xC2\xB7 "
/* D-pad keys for hints, from the extended glyph atlas: U+25C0 U+25B6, U+25B2 U+25BC */
#define KEY_LR "\xE2\x97\x80\xE2\x96\xB6"
#define KEY_UD "\xE2\x96\xB2\xE2\x96\xBC"

typedef struct {
    int icon;        /* button glyph, or -1 for a text key */
    const char *key; /* used when icon < 0 */
    u32 tint;
    const char *label;
} Hint;

/* A row of button hints centred on cx at y. */
void ui_draw_hints(const Hint *hints, int n, int cx, int y);
void ui_draw_battery(int x, int y, const SysStatus *st);
u32 ui_battery_color(const SysStatus *st); /* charging green, under 15 % red, else muted */
void ui_draw_status_bar(const char *title, const SysStatus *st); /* title left; clock, battery right */
/* Dims everything drawn so far; t = 0..256. */
void ui_dim(int t);
/* A side sheet w wide, slid in by t (0..256): its left edge, and its panel. In nuMB the
 * sheet floats: a rounded card a few pixels in from the screen's edges, with a hairline. */
int ui_sheet_x(int w, int t);
void ui_draw_sheet(int x, int w);
/* nuMB: the cross bar as the home screen draws it (Settings draws it over its own column) */
void ui_numb_draw_bar(void);
/* The hint bar the home screen and the lists use (nuMB's or XLT's, as set) */
void ui_draw_footer_hints(const Hint *hints, int n);
/* A full screen's own hint row at y: XLT's centred, or nuMB's (from the left, the quick menu at the
 * right unless quick_menu is 0). The line above it is drawn only in XLT: `if (!NUMB)`. */
void ui_draw_screen_footer(const Hint *hints, int n, int y, int quick_menu);

void ui_toast(const char *msg);
void ui_request_launch(const Entry *e);
void ui_request_exit(void);
void ui_library_changed(void); /* the library views were rebuilt */
void ui_open_music(void);       /* the Music list, from the control center */
void ui_open_removed(void);     /* the Removed list, from Settings */
void ui_arrange_home(void);     /* Arrange home menu, from Settings: ←/→ move the focused card, ✕ saves */
void ui_open_move(const Entry *e); /* Games / Homebrew: the Move to menu (tab and folder) */
/* Music (the song's options sheet): */
void ui_music_sort(int dir);                 /* the next / previous order (g_cfg.music_sort), applied and saved */
void ui_game_sort(int tab, int dir);         /* the same for Games / Homebrew (g_cfg.game_sort) */
int ui_music_in_playlist(void);              /* the Music list shows an open playlist */
int pl_row(int k);                          /* the Music list row of playlist k */
/* Add to playlist for many songs (an artist's or an album's): takes over list (malloc'd). */
void ui_open_add_many(const char *title, Entry *list, int n);
void ui_open_add_to_playlist(const Entry *e); /* the Add to playlist menu, over the sheet */
void ui_playlist_remove(const Entry *e);     /* out of the open playlist (saved) */
void ui_playlist_move(const Entry *e);       /* ↑/↓ move it in the open playlist, ✕ saves */

/* Square cover art for a song, corners rounded against bg (or a note). */
void ui_draw_cover(const Entry *e, int x, int y, int s, int r, u32 bg);

/* Formatting shared by the list preview and the details panel. */
const char *kind_name(int kind);
const char *region_name(const char *id);
void format_size(char *b, int n, uint64_t bytes);
void format_date(char *b, int n, uint32_t packed_mtime);

#endif
