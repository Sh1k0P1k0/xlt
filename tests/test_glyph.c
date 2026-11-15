/* Host test of gfx.c's text decoding (glyph_of): UTF-8, and file names in Shift-JIS
 * (fsname.h), mixed with ASCII, decode to the glyphs they should. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef struct { short x, y, w, h, ox, oy, adv; } Glyph;
typedef struct { const Glyph *glyphs; short line_h, ascent; } FontInfo;
typedef struct { short x, y, w, h; } AtlasRect;
#include "fsname.h"
#define ATLAS_DATA_IMPL
#include "atlas_data.h"
/* no PSP font here: kanji stay ? (tests/text_preview.c draws them, given XLT_PGF) */
static const Glyph *kanji_glyph(int font, uint32_t cp) { (void)font; (void)cp; return NULL; }
#include "../build/glyph_of.c" /* tests/run.sh cuts it out of src/gfx.c */

static int fails;

/* s must decode to the code points in want (0 = the '?' glyph) */
static void check(const char *what, const char *s, const unsigned *want, int n)
{
    const Glyph *q = &atlas_fonts[0].glyphs['?' - 32];
    int i = 0, ok = 1;
    while (*s) {
        int ext;
        const Glyph *g = glyph_of(0, &s, &ext);
        unsigned w = i < n ? want[i] : 0xFFFFFFFF;
        const Glyph *e = NULL;
        if (w == 0) e = q;
        else if (w >= 32 && w < 127) e = &atlas_fonts[0].glyphs[w - 32];
        else for (int k = 0; k < ATLAS_EXT_GLYPHS; k++) if (atlas_ext_cps[k] == w) e = &atlas_ext_fonts[0][k];
        if (g != e) ok = 0;
        i++;
    }
    if (i != n) ok = 0;
    printf("%s %s\n", ok ? "OK  " : "FAIL", what);
    fails += !ok;
}

int main(void)
{
    static const unsigned a[] = {0x395, 0x39B, ' ', 0x266A, ' ', 0x3B1, 'x', 0x3B1, '.'};
    check("Shift-JIS Greek after spaces and ASCII", "\x83\xa3\x83\xa9 \x81\xf4 \x83\xbfx\x83\xbf.", a, 9);
    static const unsigned b[] = {0x3B1, 0x3AC, ' ', 'c', 0xE9, ' ', 0x20AC};
    check("UTF-8 Greek, accents, euro", "\xce\xb1\xce\xac c\xc3\xa9 \xe2\x82\xac", b, 7);
    /* CE alone is a half-width katakana in Shift-JIS; E2 82 a kanji (no glyph: ?); 80 nothing */
    static const unsigned c[] = {'a', 0xFF8E, 'b', 0, ' ', 0, 'c'};
    check("broken UTF-8: Shift-JIS if it is that, else one ? each", "a\xce" "b\xe2\x82 \x80" "c", c, 7);
    static const unsigned d[] = {0x41F, 0x440, 0x438, ' ', 0x3042, 0x30A2, 0x30F3, 0x266A, 0x2605};
    check("Cyrillic, kana and symbols (UTF-8)", "\xd0\x9f\xd1\x80\xd0\xb8 \xe3\x81\x82\xe3\x82\xa2\xe3\x83\xb3\xe2\x99\xaa\xe2\x98\x85", d, 9);
    static const unsigned e[] = {0x3042, 0x30A2, 0x266A, 0x41F, 0xFF71};
    check("kana, a symbol, Cyrillic, half-width kana (Shift-JIS)", "\x82\xa0\x83\x41\x81\xf4\x84\x50\xb1", e, 5);
    static const unsigned f[] = {'A', '1', ' ', 'z'};
    check("full-width ASCII and the ideographic space drawn as ASCII", "\xef\xbc\xa1\xef\xbc\x91\xe3\x80\x80\xef\xbd\x9a", f, 4);
    static const unsigned g[] = {0xFF8A, 0xFF9D, 0xFF76, 0xFF78, ' ', 'C', 'a', 'f', 0xE9};
    check("half-width kana that look like UTF-8 (ﾝｶ = DD B6), then UTF-8 é", "\xca\xdd\xb6\xb8 Caf\xc3\xa9", g, 9);
    return fails != 0;
}
