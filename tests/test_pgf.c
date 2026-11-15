/* Host test of src/pgf.c against a PSP font file (the firmware's flash0:/font/jpn0.pgf,
 * copied off a PSP: it isn't in the repository). Draws a few characters as text and
 * checks their metrics are sane.  test_pgf FONT.pgf [TEXT-in-UTF-8] */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pgf.h"

static int file_read(void *ctx, uint32_t off, void *buf, int n)
{
    FILE *f = ctx;
    if (fseek(f, (long)off, SEEK_SET)) return -1;
    return (int)fread(buf, 1, (size_t)n, f);
}

static uint32_t utf8(const unsigned char **s)
{
    const unsigned char *p = *s;
    uint32_t c = *p++;
    int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    c &= more == 3 ? 7 : more == 2 ? 15 : more == 1 ? 31 : 127;
    while (more-- && *p) c = c << 6 | (*p++ & 63);
    *s = p;
    return c;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { printf("SKIP no font file %s\n", argv[1]); return 0; }
    Pgf f;
    if (pgf_open(&f, file_read, fp) != 0) { printf("FAIL pgf_open\n"); return 1; }
    printf("em %d px, ascender %d, %u glyphs, map from U+%04X\n", f.em_px, f.ascender, f.ptr_len, f.first);
    const char *text = argc > 2 ? argv[2] : "日本語漢字あア";
    static uint8_t pix[PGF_MAX_PIXELS];
    int bad = 0;
    for (const unsigned char *s = (const unsigned char *)text; *s;) {
        uint32_t cp = utf8(&s);
        PgfGlyph g;
        int r = pgf_glyph(&f, cp, &g, pix);
        if (r) { printf("U+%04X: %s\n", cp, r == -1 ? "not in the font" : "read error"); bad += r == -2; continue; }
        printf("U+%04X: %dx%d left %d top %d advance %.2f\n", cp, g.w, g.h, g.left, g.top, g.advance64 / 64.0);
        if (g.w <= 0 || g.h <= 0 || g.w > 40 || g.h > 40 || g.advance64 <= 0 || g.advance64 > 40 * 64) bad++;
        for (int y = 0; y < g.h; y++) {
            for (int x = 0; x < g.w; x++) putchar(" .:-=+*%#@@@@@@@"[pix[y * g.w + x]]);
            putchar('\n');
        }
    }
    PgfGlyph g;
    if (pgf_glyph(&f, 0xE000 + 0x1FFF, &g, pix) != -1) { printf("FAIL a private-use code point was found\n"); bad++; }
    pgf_close(&f);
    fclose(fp);
    printf(bad ? "FAIL\n" : "OK   test_pgf\n");
    return bad != 0;
}
