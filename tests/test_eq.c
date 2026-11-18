/* The music equalizer (src/eq.c): sines through the fixed-point filters come
 * out at the level the designed curve says, full-scale ones never clip, a flat
 * curve leaves the samples alone, and odd sample rates stay stable. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/eq.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); puts(""); fails++; } } while (0)

#define N 48000
static int16_t buf[N * 2];

/* A sine of amplitude amp at hz through the EQ (in blocks of 1152, as MP3 does);
 * returns the output's level against the input's in dB, over the last half. */
static double sine_gain(int rate, const int g[EQ_BANDS], double hz, double amp, int *clipped)
{
    Eq eq;
    memset(&eq, 0, sizeof(eq));
    for (int i = 0; i < N; i++)
        buf[i * 2] = buf[i * 2 + 1] = (int16_t)lrint(amp * sin(2 * M_PI * hz * i / rate));
    for (int at = 0; at < N; at += 1152)
        eq_process(&eq, buf + at * 2, at + 1152 <= N ? 1152 : N - at, rate, g);
    double in = 0, out = 0;
    int cl = 0;
    for (int i = N / 2; i < N; i++) {
        double s = amp * sin(2 * M_PI * hz * i / rate);
        in += s * s;
        out += (double)buf[i * 2] * buf[i * 2];
        if (buf[i * 2] == 32767 || buf[i * 2] == -32768) cl++;
        if (buf[i * 2] != buf[i * 2 + 1]) cl += 1000000; /* the channels must match */
    }
    if (clipped) *clipped = cl;
    return 10 * log10(out / in);
}

/* What the curve says, with the overall cut the EQ applies (its top at 0 dB). */
static double expect(int rate, const int g[EQ_BANDS], double hz)
{
    double top = 0, hi = rate * 0.49 < 20000 ? rate * 0.49 : 20000;
    for (int i = 0; i <= 96; i++) {
        double d = eq_response_db(rate, g, (float)(20 * pow(hi / 20, i / 96.0)));
        if (d > top) top = d;
    }
    for (int b = 0; b < EQ_BANDS; b++)
        if (eq_band_hz(b) < hi && eq_response_db(rate, g, (float)eq_band_hz(b)) > top)
            top = eq_response_db(rate, g, (float)eq_band_hz(b));
    return eq_response_db(rate, g, (float)hz) - top;
}

int main(void)
{
    /* the curve hits its gains at the band centres (peaks), and the shelves reach theirs */
    for (int b = 1; b < 4; b++) {
        int g[EQ_BANDS] = {0};
        g[b] = 9;
        float d = eq_response_db(44100, g, (float)eq_band_hz(b));
        CHECK(fabsf(d - 9) < 0.05f, "band %d peak %.2f dB", b, d);
    }
    { int g[EQ_BANDS] = {12, 0, 0, 0, 0}; float d = eq_response_db(44100, g, 20);
      CHECK(fabsf(d - 12) < 0.5f, "low shelf at 20 Hz %.2f", d); }
    { int g[EQ_BANDS] = {0, 0, 0, 0, -12}; float d = eq_response_db(44100, g, 20000);
      CHECK(fabsf(d + 12) < 1.0f, "high shelf at 20 kHz %.2f", d); }

    /* flat: nothing touched, and it says so */
    {
        Eq eq; memset(&eq, 0, sizeof(eq));
        int g[EQ_BANDS] = {0};
        for (int i = 0; i < 2304; i++) buf[i] = (int16_t)(i * 37);
        int16_t copy[2304]; memcpy(copy, buf, sizeof(copy));
        CHECK(eq_process(&eq, buf, 1152, 44100, g) == 0, "flat EQ ran");
        CHECK(!memcmp(copy, buf, sizeof(copy)), "flat EQ changed the samples");
    }

    /* the fixed-point filters do what the curve says, for every preset and some harsh curves */
    static const int rates[] = {44100, 48000, 32000, 22050, 8000};
    static const double freqs[] = {40, 60, 120, 250, 500, 1000, 2000, 4000, 7000, 12000, 15000};
    int curves[EQ_PRESETS + 3][EQ_BANDS];
    int nc = 0;
    int zero[EQ_BANDS] = {0};
    for (int p = EQ_BASS; p < EQ_CUSTOM; p++) eq_gains(p, zero, curves[nc++]);
    int harsh[3][EQ_BANDS] = {{12, 12, 12, 12, 12}, {-12, 12, -12, 12, -12}, {12, -12, 0, -12, 12}};
    for (int i = 0; i < 3; i++) memcpy(curves[nc++], harsh[i], sizeof(harsh[i]));
    double worst = 0;
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < nc; c++)
            for (int f = 0; f < 11; f++) {
                int rate = rates[r];
                double hz = freqs[f];
                if (hz > rate * 0.45) continue;
                int cl;
                double got = sine_gain(rate, curves[c], hz, 32000, &cl);
                double want = expect(rate, curves[c], hz);
                CHECK(fabs(got - want) < 0.2, "rate %d curve %d at %.0f Hz: %.2f dB, curve says %.2f", rate, c, hz, got, want);
                CHECK(cl == 0, "rate %d curve %d at %.0f Hz: %d samples clipped (or channels differ)", rate, c, hz, cl);
                if (fabs(got - want) > worst) worst = fabs(got - want);
            }
    /* quiet sounds stay clean too (rounding noise far below the signal) */
    {
        int g[EQ_BANDS] = {6, 3, 0, 0, 0};
        double got = sine_gain(48000, g, 60, 30, NULL), want = expect(48000, g, 60);
        CHECK(fabs(got - want) < 0.5, "quiet 60 Hz: %.2f dB, curve says %.2f", got, want);
    }
    /* presets by name, both ways */
    for (int p = 0; p < EQ_PRESETS; p++)
        CHECK(eq_preset_parse(eq_preset_key(p)) == p, "preset %d key", p);
    CHECK(eq_preset_parse("nonsense") == EQ_OFF, "unknown key");

    if (fails) { printf("test_eq: %d failed\n", fails); return 1; }
    printf("test_eq: ok (worst %.3f dB off the curve)\n", worst);
    return 0;
}
