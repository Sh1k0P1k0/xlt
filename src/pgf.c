/* XLT - the PSP's own fonts (PGF). See pgf.h. */
#include <stdlib.h>
#include <string.h>
#include "pgf.h"

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* n bits (<= 32) at bit pos of a little-endian bit stream, low bits first */
static uint32_t bits(const uint8_t *buf, uint32_t pos, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++, pos++)
        v |= (uint32_t)((buf[pos >> 3] >> (pos & 7)) & 1) << i;
    return v;
}

void pgf_close(Pgf *f)
{
    free(f->ptrs);
    for (int i = 0; i < 4; i++)
        free(f->tab[i]);
    memset(f, 0, sizeof(*f));
}

int pgf_open(Pgf *f, PgfRead read, void *ctx)
{
    memset(f, 0, sizeof(*f));
    f->read = read;
    f->ctx = ctx;
    uint8_t h[0x188];
    if (read(ctx, 0, h, sizeof(h)) != (int)sizeof(h) || memcmp(h + 4, "PGF0", 4))
        return -1;
    uint32_t hsize = le16(h + 2), rev = le32(h + 8);
    uint32_t map_len = le32(h + 16), ptr_len = le32(h + 20), map_bpe = le32(h + 24), ptr_bpe = le32(h + 28);
    int32_t hres = (int32_t)le32(h + 44), hsz = (int32_t)le32(h + 36);
    if (hsize < sizeof(h) || map_bpe == 0 || map_bpe > 32 || ptr_bpe == 0 || ptr_bpe > 32 ||
        map_len > 0x100000 || ptr_len == 0 || ptr_len > 0x100000 || hres <= 0)
        return -1;
    f->first = le16(h + 182);
    f->map_len = map_len;
    f->map_bpe = map_bpe;
    f->ptr_len = ptr_len;
    f->ptr_bpe = ptr_bpe;
    f->em_px = (int)((int64_t)hsz * hres / (72 * 64 * 64)); /* points (26.6) at hres dpi (26.6) */
    f->ascender = (int)(int32_t)le32(h + 212) / 64;
    /* the metric tables' lengths, then the shadow map's */
    memcpy(f->ntab, h + 258, 4);
    uint32_t shadow_len = le32(h + 364), shadow_bpe = le32(h + 368);
    if (shadow_bpe > 32 || shadow_len > 0x10000)
        return -1;

    uint32_t off = hsize;
    if (rev == 3) {                            /* a compressed char map: not in the firmware's fonts */
        return -1;
    }
    for (int t = 0; t < 4; t++) {
        uint32_t n = f->ntab[t];
        if (!n) continue;
        f->tab[t] = malloc(n * 8);
        if (!f->tab[t]) return -1;
        uint8_t *raw = (uint8_t *)f->tab[t];
        if (read(ctx, off, raw, (int)(n * 8)) != (int)(n * 8))
            return -1;
        for (uint32_t i = 0; i < n * 2; i++)
            ((int32_t *)f->tab[t])[i] = (int32_t)le32(raw + i * 4);
        off += n * 8;
    }
    off += (shadow_len * shadow_bpe + 31) / 32 * 4;  /* the shadow map: XLT draws no shadows */
    f->map_off = off;
    off += (map_len * map_bpe + 31) / 32 * 4;
    uint32_t ptr_bytes = (ptr_len * ptr_bpe + 31) / 32 * 4;
    f->ptrs = malloc(ptr_bytes);
    if (!f->ptrs || read(ctx, off, f->ptrs, (int)ptr_bytes) != (int)ptr_bytes)
        return -1;
    f->data_off = off + ptr_bytes;
    return 0;
}

/* entry i of a bit-packed table that starts at file offset base: read through the file */
static int file_entry(const Pgf *f, uint32_t base, uint32_t i, uint32_t bpe, uint32_t *out)
{
    uint32_t bit = i * bpe;
    uint8_t b[8];
    if (f->read(f->ctx, base + (bit >> 3), b, 8) < (int)((bit & 7) + bpe + 7) / 8)
        return -1;
    *out = bits(b, bit & 7, (int)bpe);
    return 0;
}

int pgf_glyph(const Pgf *f, uint32_t cp, PgfGlyph *g, uint8_t *pix)
{
    if (cp < f->first || cp - f->first >= f->map_len)
        return -1;
    uint32_t idx;
    if (file_entry(f, f->map_off, cp - f->first, f->map_bpe, &idx) < 0)
        return -2;
    if (idx >= f->ptr_len)
        return -1;
    uint32_t ptr = bits((const uint8_t *)f->ptrs, idx * f->ptr_bpe, (int)f->ptr_bpe);

    /* the glyph: a header of bit fields, then its pixels as runs of nibbles */
    static uint8_t buf[2600];
    int got = f->read(f->ctx, f->data_off + ptr * 4, buf, sizeof(buf));
    if (got < 64)
        return got < 0 ? -2 : -1;
    uint32_t p = 14; /* its size: not needed */
    int w = (int)bits(buf, p, 7), h = (int)bits(buf, p + 7, 7);
    int left = (int)bits(buf, p + 14, 7), top = (int)bits(buf, p + 21, 7);
    int flags = (int)bits(buf, p + 28, 6);
    p += 34 + 7 + 9; /* shadow flags and id */
    if (left >= 64) left -= 128;
    if (top >= 64) top -= 128;
    int32_t adv64 = 0;
    static const int MASK[4] = {0x04, 0x08, 0x10, 0x20}; /* the value is an index into its table */
    for (int t = 0; t < 4; t++) {
        int32_t v;
        if (flags & MASK[t]) {
            uint32_t i = bits(buf, p, 8);
            p += 8;
            v = i < f->ntab[t] ? f->tab[t][i][0] : 0;
        } else {
            v = (int32_t)bits(buf, p, 32);
            p += 64; /* horizontal, then vertical */
        }
        if (t == 3)
            adv64 = v;
    }
    if (w * h > PGF_MAX_PIXELS || ((flags & 3) != 1 && (flags & 3) != 2))
        return -1;
    g->w = w;
    g->h = h;
    g->left = left;
    g->top = top;
    g->advance64 = adv64;

    /* runs: a nibble n < 8 is n+1 copies of the next nibble; n >= 8 is 16-n nibbles as they are */
    static uint8_t raw[PGF_MAX_PIXELS];
    int n = w * h, k = 0;
    uint32_t end = (uint32_t)got * 8;
    while (k < n && p + 4 <= end) {
        int c = (int)bits(buf, p, 4);
        p += 4;
        if (c < 8) {
            if (p + 4 > end) break;
            int v = (int)bits(buf, p, 4);
            p += 4;
            for (int i = 0; i <= c && k < n; i++)
                raw[k++] = (uint8_t)v;
        } else {
            for (int i = 0; i < 16 - c && k < n && p + 4 <= end; i++, p += 4)
                raw[k++] = (uint8_t)bits(buf, p, 4);
        }
    }
    while (k < n)
        raw[k++] = 0;
    if ((flags & 3) == 1) {
        memcpy(pix, raw, (size_t)n);
    } else { /* stored column by column */
        for (int x = 0; x < w; x++)
            for (int y = 0; y < h; y++)
                pix[y * w + x] = raw[x * h + y];
    }
    return 0;
}
