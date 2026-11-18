/* XLT - the music equalizer: five bands (a low shelf, three peaks, a high
 * shelf), ±12 dB each, in fixed point on the CPU, applied to every block the
 * music player hands to the SRC channel (MP3, AAC, FLAC alike).
 *
 * The whole sound is turned down by the curve's highest point first, so a
 * boost never clips: "Bass" plays a little quieter overall, with more bass.
 * Bands at 0 dB are skipped, and Off (or a curve all at 0) costs nothing.
 *
 * No PSP headers: tests/test_eq.c runs it on the PC. */
#ifndef EQ_H
#define EQ_H

#include <stdint.h>

#define EQ_BANDS 5
#define EQ_MAX_DB 12

enum { EQ_OFF, EQ_BASS, EQ_TREBLE, EQ_VOCAL, EQ_LOUD, EQ_CUSTOM, EQ_PRESETS };

const char *eq_preset_name(int p);   /* "Bass" */
const char *eq_preset_key(int p);    /* "bass", as xlt.ini spells it */
int eq_preset_parse(const char *key); /* EQ_OFF if unknown */
/* The preset's gains in dB; Custom's are the user's own (custom). */
void eq_gains(int preset, const int custom[EQ_BANDS], int out[EQ_BANDS]);
int eq_band_hz(int b);
const char *eq_band_label(int b);    /* "60 Hz", "1 kHz" */

typedef struct {
    int32_t b0, b1, b2, a1, a2; /* Q28 */
} EqCoef;

typedef struct {
    int rate;                   /* what the coefficients were made for; 0 = not yet */
    int gains[EQ_BANDS];
    int used;                   /* bands not at 0 dB, in order */
    int band[EQ_BANDS];         /* which band each of those is */
    int32_t pre;                /* Q15: the cut that keeps the curve's top at 0 dB */
    EqCoef c[EQ_BANDS];
    int32_t st[EQ_BANDS][2][5]; /* per band and channel: x1, x2, y1, y2 (Q8), the rounding left over */
} Eq;

/* The gain in dB that the curve has at hz (for drawing it, and the cut). */
float eq_response_db(int rate, const int gains[EQ_BANDS], float hz);

/* Filters n stereo frames in place, at rate Hz with these gains (worked out
 * again when either changes; the filters' memory is kept unless the rate
 * changes). Returns 0 and leaves pcm alone when every gain is 0. */
int eq_process(Eq *eq, int16_t *pcm, int n, int rate, const int gains[EQ_BANDS]);
void eq_reset(Eq *eq); /* forget the filters' memory (a new song, a seek) */

#endif
