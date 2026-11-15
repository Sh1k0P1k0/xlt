/* XLT - the PSP's own fonts (PGF, as in flash0:/font/jpn0.pgf). Portable (host-tested).
 *
 * XLT draws kanji from the firmware's Japanese font instead of carrying one:
 * every PSP has it. Only the small tables stay in memory (the glyph pointers,
 * ~23 KB for jpn0.pgf, and the metric tables); a character's map entry and its
 * glyph are read from the file when it is first needed. The format follows
 * PPSSPP's Core/Font/PGF.cpp (GPL-2.0 or later), itself after JPCSP. */
#ifndef PGF_H
#define PGF_H

#include <stdint.h>

/* Reads n bytes at offset off of the font file into buf: bytes read, < 0 on error. */
typedef int (*PgfRead)(void *ctx, uint32_t off, void *buf, int n);

typedef struct {
    PgfRead read;
    void *ctx;
    uint32_t first, map_len, map_bpe, map_off; /* the char map: read on demand */
    uint32_t ptr_len, ptr_bpe;
    uint32_t *ptrs;                            /* the glyph pointer table (bit-packed, as in the file) */
    uint32_t data_off;                         /* where the glyphs start */
    uint8_t ntab[4];                           /* dimension, x, y, advance table lengths */
    int32_t (*tab[4])[2];                      /* those tables: [index][0 = horizontal, 1 = vertical] */
    int em_px;                                 /* the size the glyphs are drawn at, in pixels per em */
    int ascender;                              /* in pixels */
} Pgf;

typedef struct {
    int w, h;       /* the bitmap */
    int left, top;  /* from the pen: right to the bitmap's left edge, up from the baseline to its top */
    int advance64;  /* the pen moves this far, in 1/64 pixels */
} PgfGlyph;

#define PGF_MAX_PIXELS (64 * 64)

/* Reads the header and tables. 0 on success; pgf_close frees them either way. */
int pgf_open(Pgf *f, PgfRead read, void *ctx);
void pgf_close(Pgf *f);

/* The glyph for code point cp: its metrics, and its pixels (w*h values 0..15, rows
 * top to bottom) into pix (room for PGF_MAX_PIXELS). 0 if found, -1 if the font
 * hasn't it, -2 on a read error. */
int pgf_glyph(const Pgf *f, uint32_t cp, PgfGlyph *g, uint8_t *pix);

#endif
