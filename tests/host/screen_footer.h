/* XLT tests - a stand-in for ui.c's ui_draw_screen_footer (nuMB N5), for the previews that
 * draw a screen without ui.c: XLT's centred row through the preview's own ui_draw_hints, or
 * nuMB's row from the left with the quick menu at the right, laid out as ui.c lays it out. */
#ifndef SCREEN_FOOTER_H
#define SCREEN_FOOTER_H

static int sf_hint(const Hint *h, int x, int y)
{
    if (h->icon >= 0) {
        gfx_icon(h->icon, x, y + 3, 12, 12, btn_tint(h->tint));
        x += 12 + 4;
    } else {
        x = gfx_text(FONT_SMALL, x, y, h->key, C_TEXT) + 4;
    }
    return gfx_text(FONT_SMALL, x, y, h->label, C_MUTED);
}

void ui_draw_screen_footer(const Hint *hints, int n, int y, int quick_menu)
{
    if (!NUMB) {
        ui_draw_hints(hints, n, SCREEN_W / 2, y);
        return;
    }
    int x = 12;
    for (int i = 0; i < n; i++)
        if (hints[i].icon >= 0 || strcmp(hints[i].key, "SELECT"))
            x = sf_hint(&hints[i], x, y) + 16;
    static const Hint q = {-1, "SELECT", 0, "Quick menu"};
    int w = gfx_text_width(FONT_SMALL, q.key) + 4 + gfx_text_width(FONT_SMALL, q.label);
    if (quick_menu && x + w <= SCREEN_W - 12) /* only when there's room, as in ui.c */
        sf_hint(&q, SCREEN_W - 12 - w, y);
}

#endif
