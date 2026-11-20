/* XLT - gapless playback and crossfades: padding, resampling, mixing (see gapless.h). */
#include "gapless.h"
#include <math.h>
#include <string.h>

#define DECODER_DELAY 529 /* every MP3 decoder's (528 + 1), as LAME and ffmpeg count it */

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

int gap_lame(const uint8_t *b, int n, GapInfo *g)
{
    if (n < 4)
        return 0;
    uint32_t h = be32(b);
    if ((h & 0xFFE00000) != 0xFFE00000)
        return 0;
    int ver = (h >> 19) & 3, mono = ((h >> 6) & 3) == 3;
    int off = ver == 3 ? (mono ? 21 : 36) : (mono ? 13 : 21);
    if (off + 8 > n || (memcmp(b + off, "Xing", 4) && memcmp(b + off, "Info", 4)))
        return 0;
    uint32_t flags = be32(b + off + 4);
    int p = off + 8;
    if (flags & 1) p += 4;   /* frames */
    if (flags & 2) p += 4;   /* bytes */
    if (flags & 4) p += 100; /* TOC */
    if (flags & 8) p += 4;   /* quality */
    if (p + 24 > n)
        return 0;
    const uint8_t *t = b + p;
    if (memcmp(t, "LAME", 4) && memcmp(t, "Lavc", 4) && memcmp(t, "Lavf", 4) && memcmp(t, "L3.9", 4))
        return 0;
    /* 9 bytes encoder, revision, lowpass, 8 bytes ReplayGain, flags, bitrate, then 12 + 12 bits */
    int delay = t[21] << 4 | t[22] >> 4, pad = (t[22] & 15) << 8 | t[23];
    if (!delay && !pad)
        return 0; /* not filled in */
    g->delay = delay + DECODER_DELAY;
    g->pad = pad > DECODER_DELAY ? pad - DECODER_DELAY : 0;
    return 1;
}

static int hexword(const char **s, uint64_t *out)
{
    const char *p = *s;
    while (*p == ' ')
        p++;
    uint64_t v = 0;
    int digits = 0;
    for (;; p++, digits++) {
        int c = *p, d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                      : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0)
            break;
        if (digits >= 16)
            return -1;
        v = v << 4 | (uint64_t)d;
    }
    if (!digits || (*p && *p != ' '))
        return -1;
    *out = v;
    *s = p;
    return 0;
}

int gap_itunsmpb(const char *v, int *delay, int *pad, uint64_t *total)
{
    uint64_t w[4];
    const char *p = v;
    for (int i = 0; i < 4; i++)
        if (hexword(&p, &w[i]) != 0)
            return -1;
    if (w[1] > 0xFFFF || w[2] > 0xFFFF || (!w[1] && !w[2]))
        return -1;
    *delay = (int)w[1];
    *pad = (int)w[2];
    *total = w[3];
    return 0;
}

int gap_from_edit(int64_t skip, uint64_t len, uint32_t ts, int rate, uint64_t frames, GapInfo *g)
{
    if (!ts || rate <= 0 || skip < 0)
        return -1;
    uint64_t s = (uint64_t)skip * (uint64_t)rate / ts, l = len * (uint64_t)rate / ts;
    if (s > 0xFFFF)
        return -1;
    int pad = 0;
    if (l && frames > s + l && frames - s - l < 0xFFFF)
        pad = (int)(frames - s - l);
    if (!s && !pad)
        return -1;
    g->delay = (int)s;
    g->pad = pad;
    return 0;
}

/* ------------------------------------------------------------ resampling */

void rs_init(Resampler *r, int in_rate, int out_rate)
{
    memset(r, 0, sizeof(*r));
    r->step = (uint32_t)(((uint64_t)in_rate << 16) / (uint32_t)(out_rate > 0 ? out_rate : 1));
    r->phase = 1u << 16; /* the first frame is taken in before anything is made */
}

int rs_need(const Resampler *r, int nout)
{
    uint64_t need = ((uint64_t)r->phase + (uint64_t)r->step * (uint64_t)nout) >> 16;
    return (int)need + 1;
}

static int16_t clip16(float v)
{
    return v > 32767.f ? 32767 : v < -32768.f ? -32768 : (int16_t)lrintf(v);
}

int rs_run(Resampler *r, const int16_t *in, int nin, int16_t *out, int nout, int *used)
{
    int made = 0, u = 0;
    while (made < nout) {
        while (r->phase >= 1u << 16) {
            if (u >= nin)
                goto done;
            if (!r->primed) {
                for (int k = 0; k < 4; k++)
                    r->h[k][0] = in[0], r->h[k][1] = in[1];
                r->primed = 1;
            } else {
                memmove(r->h[0], r->h[1], sizeof(r->h[0]) * 3);
                r->h[3][0] = in[u * 2];
                r->h[3][1] = in[u * 2 + 1];
            }
            u++;
            r->phase -= 1u << 16;
        }
        float t = (float)r->phase * (1.f / 65536.f);
        for (int c = 0; c < 2; c++) {
            float p0 = r->h[0][c], p1 = r->h[1][c], p2 = r->h[2][c], p3 = r->h[3][c];
            float v = p1 + 0.5f * t * (p2 - p0 + t * (2.f * p0 - 5.f * p1 + 4.f * p2 - p3 + t * (3.f * (p1 - p2) + p3 - p0)));
            out[made * 2 + c] = clip16(v);
        }
        made++;
        r->phase += r->step;
    }
done:
    *used = u;
    return made;
}

/* ---------------------------------------------------------------- mixing */

#define XF_STEPS 256
static int s_curve[XF_STEPS + 1];

int xf_gain(int pos, int len)
{
    if (!s_curve[XF_STEPS])
        for (int i = 0; i <= XF_STEPS; i++)
            s_curve[i] = (int)lrintf(sinf((float)i * 3.14159265f / (2.f * XF_STEPS)) * Q15);
    if (len <= 0 || pos >= len)
        return Q15;
    if (pos <= 0)
        return 0;
    int64_t x = (int64_t)pos * XF_STEPS * 256 / len; /* 8 bits of fraction between steps */
    int i = (int)(x >> 8), f = (int)(x & 255);
    return s_curve[i] + (((s_curve[i < XF_STEPS ? i + 1 : i] - s_curve[i]) * f) >> 8);
}

void mix_add(int32_t *acc, const int16_t *src, int n, int g0, int g1)
{
    if (g0 == g1) {
        for (int i = 0; i < n * 2; i++)
            acc[i] += (src[i] * g0) >> 15;
        return;
    }
    for (int i = 0; i < n; i++) {
        int g = g0 + (int)((int64_t)(g1 - g0) * i / n);
        acc[i * 2] += (src[i * 2] * g) >> 15;
        acc[i * 2 + 1] += (src[i * 2 + 1] * g) >> 15;
    }
}

void mix_out(int16_t *dst, const int32_t *acc, int n)
{
    for (int i = 0; i < n * 2; i++)
        dst[i] = (int16_t)(acc[i] > 32767 ? 32767 : acc[i] < -32768 ? -32768 : acc[i]);
}

/* ------------------------------------------------------------ the album rule */

int gap_same_album(const GapSong *a, const GapSong *b)
{
    if (!a->album || !a->album[0] || !b->album || !b->album[0] || strcmp(a->album, b->album))
        return 0; /* untagged songs are taken as singles: a folder of them is no album */
    return a->track && b->track ? b->track == a->track + 1 : b->index == a->index + 1;
}

int gap_fade_ms(int fade_s, int repeat_one, int same_album, int a_left_ms, int b_ms)
{
    if (fade_s <= 0 || repeat_one || same_album || a_left_ms <= 0)
        return 0;
    int ms = fade_s * 1000;
    if (ms > a_left_ms) ms = a_left_ms;
    if (b_ms > 0 && ms > b_ms / 2) ms = b_ms / 2;
    return ms < 200 ? 0 : ms; /* too short to be heard as a fade: just join them */
}
