/* XLT - the music equalizer. See eq.h.
 *
 * Each band is a biquad from the Audio EQ Cookbook (R. Bristow-Johnson): a low
 * shelf at 60 Hz, peaks (Q 1, about 1.4 octaves wide) at 250 Hz, 1 kHz and
 * 4 kHz, a high shelf at 12 kHz. The coefficients are made in float when the
 * gains or the sample rate change, then run in Direct Form I in integers:
 * Q28 coefficients, samples with 8 bits below the point between the bands,
 * 64-bit sums (Allegrex has a multiply-accumulate). What each sum's rounding
 * drops is added to the next one (error feedback): without it the bass shelf,
 * whose poles sit next to 1, loses quiet sounds (-60 dB came out 2.4 dB low).
 * Form I takes coefficient
 * changes mid-song without a thump, which Form II doesn't always.
 *
 * Cost per stereo frame: 10 multiply-adds per band in use, about 1 MHz of
 * the CPU's 222 for each band at 44.1 kHz. */
#include <math.h>
#include <string.h>
#include "eq.h"

#define PI_F 3.14159265f

static const int BAND_HZ[EQ_BANDS] = {60, 250, 1000, 4000, 12000};
static const char *const BAND_LABEL[EQ_BANDS] = {"60 Hz", "250 Hz", "1 kHz", "4 kHz", "12 kHz"};

static const struct {
    const char *name, *key;
    signed char db[EQ_BANDS];
} PRESETS[EQ_PRESETS] = {
    {"Off", "off", {0, 0, 0, 0, 0}},
    {"Bass", "bass", {6, 3, 0, 0, 0}},
    {"Treble", "treble", {0, 0, 0, 3, 6}},
    {"Vocal", "vocal", {-2, -1, 3, 3, 0}},
    {"Loudness", "loudness", {6, 2, 0, 2, 5}},
    {"Custom", "custom", {0, 0, 0, 0, 0}},
};

const char *eq_preset_name(int p) { return PRESETS[p >= 0 && p < EQ_PRESETS ? p : 0].name; }
const char *eq_preset_key(int p) { return PRESETS[p >= 0 && p < EQ_PRESETS ? p : 0].key; }
int eq_band_hz(int b) { return BAND_HZ[b]; }
const char *eq_band_label(int b) { return BAND_LABEL[b]; }

int eq_preset_parse(const char *key)
{
    for (int i = 0; i < EQ_PRESETS; i++)
        if (!strcmp(key, PRESETS[i].key))
            return i;
    return EQ_OFF;
}

static int clamp_db(int g) { return g < -EQ_MAX_DB ? -EQ_MAX_DB : g > EQ_MAX_DB ? EQ_MAX_DB : g; }

void eq_gains(int preset, const int custom[EQ_BANDS], int out[EQ_BANDS])
{
    for (int b = 0; b < EQ_BANDS; b++)
        out[b] = preset == EQ_CUSTOM ? clamp_db(custom[b]) : PRESETS[preset >= 0 && preset < EQ_PRESETS ? preset : 0].db[b];
}

/* One band's coefficients, normalised (a0 = 1): b0 b1 b2 a1 a2. */
static void design(int band, int db, int rate, float c[5])
{
    float f = (float)BAND_HZ[band];
    if (f > rate * 0.42f) f = rate * 0.42f; /* a 22 or 24 kHz song has no 12 kHz to speak of */
    float A = powf(10.0f, db / 40.0f), w = 2 * PI_F * f / rate, cw = cosf(w), sw = sinf(w);
    float b0, b1, b2, a0, a1, a2;
    if (band == 0 || band == EQ_BANDS - 1) {
        float al = sw / 2 * sqrtf(2.0f), k = 2 * sqrtf(A) * al; /* shelf slope 1 */
        float s = band == 0 ? 1.0f : -1.0f;                    /* low shelf / high shelf */
        b0 = A * ((A + 1) - s * (A - 1) * cw + k);
        b1 = s * 2 * A * ((A - 1) - s * (A + 1) * cw);
        b2 = A * ((A + 1) - s * (A - 1) * cw - k);
        a0 = (A + 1) + s * (A - 1) * cw + k;
        a1 = -s * 2 * ((A - 1) + s * (A + 1) * cw);
        a2 = (A + 1) + s * (A - 1) * cw - k;
    } else {
        float al = sw / 2; /* Q = 1 */
        b0 = 1 + al * A;
        b1 = -2 * cw;
        b2 = 1 - al * A;
        a0 = 1 + al / A;
        a1 = -2 * cw;
        a2 = 1 - al / A;
    }
    c[0] = b0 / a0;
    c[1] = b1 / a0;
    c[2] = b2 / a0;
    c[3] = a1 / a0;
    c[4] = a2 / a0;
}

/* |H| of one biquad at angle w, squared. */
static float mag2(const float c[5], float w)
{
    float c1 = cosf(w), s1 = sinf(w), c2 = cosf(2 * w), s2 = sinf(2 * w);
    float nr = c[0] + c[1] * c1 + c[2] * c2, ni = -(c[1] * s1 + c[2] * s2);
    float dr = 1 + c[3] * c1 + c[4] * c2, di = -(c[3] * s1 + c[4] * s2);
    return (nr * nr + ni * ni) / (dr * dr + di * di);
}

float eq_response_db(int rate, const int gains[EQ_BANDS], float hz)
{
    float w = 2 * PI_F * hz / rate, m = 1;
    for (int b = 0; b < EQ_BANDS; b++)
        if (gains[b]) {
            float c[5];
            design(b, gains[b], rate, c);
            m *= mag2(c, w);
        }
    return 10 * log10f(m);
}

/* The curve's highest point in dB (0 if it only cuts): what the sound is turned down by. */
static float peak_db(int rate, const int gains[EQ_BANDS])
{
    float top = 0, hi = rate * 0.49f < 20000 ? rate * 0.49f : 20000;
    for (int i = 0; i <= 96; i++) {
        float d = eq_response_db(rate, gains, 20 * powf(hi / 20, i / 96.0f));
        if (d > top) top = d;
    }
    for (int b = 0; b < EQ_BANDS; b++) {
        float d = BAND_HZ[b] < hi ? eq_response_db(rate, gains, (float)BAND_HZ[b]) : 0;
        if (d > top) top = d;
    }
    return top;
}

static int32_t q28(float v) { return (int32_t)lrintf(v * (float)(1 << 28)); }

static void make(Eq *eq, int rate, const int gains[EQ_BANDS])
{
    if (rate != eq->rate)
        memset(eq->st, 0, sizeof(eq->st));
    eq->used = 0;
    for (int b = 0; b < EQ_BANDS; b++) {
        if (!gains[b])
            continue;
        if (!eq->gains[b] || rate != eq->rate) /* coming in: no memory from long ago */
            memset(eq->st[b], 0, sizeof(eq->st[b]));
        float c[5];
        design(b, gains[b], rate, c);
        EqCoef *k = &eq->c[eq->used];
        k->b0 = q28(c[0]);
        k->b1 = q28(c[1]);
        k->b2 = q28(c[2]);
        k->a1 = q28(c[3]);
        k->a2 = q28(c[4]);
        eq->band[eq->used++] = b;
    }
    eq->pre = (int32_t)lrintf(32768.0f / powf(10.0f, peak_db(rate, gains) / 20));
    eq->rate = rate;
    memcpy(eq->gains, gains, sizeof(eq->gains));
}

void eq_reset(Eq *eq) { memset(eq->st, 0, sizeof(eq->st)); }

int eq_process(Eq *eq, int16_t *pcm, int n, int rate, const int gains[EQ_BANDS])
{
    if (rate <= 0)
        return 0;
    if (rate != eq->rate || memcmp(gains, eq->gains, sizeof(eq->gains)))
        make(eq, rate, gains);
    if (!eq->used)
        return 0;
    const int32_t pre = eq->pre;
    const int used = eq->used;
    for (int i = 0; i < n; i++)
        for (int ch = 0; ch < 2; ch++) {
            int32_t x = ((int32_t)pcm[i * 2 + ch] * pre) >> 7; /* Q15 gain, Q8 out */
            for (int u = 0; u < used; u++) {
                const EqCoef *k = &eq->c[u];
                int32_t *s = eq->st[eq->band[u]][ch];
                int64_t acc = (int64_t)k->b0 * x + (int64_t)k->b1 * s[0] + (int64_t)k->b2 * s[1] -
                              (int64_t)k->a1 * s[2] - (int64_t)k->a2 * s[3] + s[4];
                int32_t y = (int32_t)(acc >> 28);
                s[4] = (int32_t)(acc - (int64_t)y * (1 << 28)); /* carried to the next sample */
                s[1] = s[0];
                s[0] = x;
                s[3] = s[2];
                s[2] = y;
                x = y;
            }
            x = (x + 128) >> 8;
            pcm[i * 2 + ch] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
        }
    return 1;
}
