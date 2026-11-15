/* Renders text through the real gfx.c (the software GE of saver_preview.c): the
 * extended glyphs (Latin-1, Greek, Cyrillic, kana, symbols) in the three fonts,
 * from UTF-8 and from Shift-JIS file names, and kanji from the PSP's font if XLT_PGF
 * names a copy of it. Writes text.ppm.
 *
 *   text_preview OUTDIR */
#define main saver_main
#include "saver_preview.c"
#undef main

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    dm = gfx_dither_matrix;
    sceGuTexMode(GU_PSM_4444, 0, 0, GU_TRUE);
    sceGuTexImage(0, ATLAS_W, ATLAS_H, ATLAS_W, NULL);
    static const char *lines[] = {
        "Café, naïve, Ελληνικά, Привет мир",
        "\xe3\x81\x82\xe3\x81\x84\xe3\x81\x86 \xe3\x82\xab\xe3\x82\xbf\xe3\x82\xab\xe3\x83\x8a \xef\xbe\x8a\xef\xbe\x9d\xef\xbd\xb6\xef\xbd\xb8 \xe2\x99\xaa\xe2\x98\x85\xe2\x86\x92 \xef\xbc\xa1\xef\xbc\xa2\xef\xbc\xa3\xef\xbc\x91\xef\xbc\x92",
        "SJIS: \x83\xa3\x83\xc9\x83\xc9\x83\xc5\x83\xcb\x83\xc7\x83\xc8\x83\xbf \x81\xf4 \x84\x50\x84\x91\x84\x89 \x82\xa0\x83\x41",
        "kanji: \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\xe3\x81\xae\xe6\xbc\xa2\xe5\xad\x97 \x93\xfa\x96\x7b (SJIS)",
    };
    gfx_begin(RGB(0x0d1117));
    int y = 12;
    for (int f = 0; f < FONT_COUNT; f++)
        for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i++, y += gfx_line_h(f))
            gfx_text(f, 10, y, lines[i], f == FONT_BOLD ? RGB(0xffcc66) : RGB(0xe6edf3));
    gfx_end();
    save(dir, "text");
    printf("text_preview: ok\n");
    return 0;
}
