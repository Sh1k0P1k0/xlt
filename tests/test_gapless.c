/* Host test for src/gapless.c: encoder padding from real LAME / ffmpeg files
 * (made by tests/run.sh into build/gapless/), iTunSMPB, the MP4 edit list, the
 * resampler, the crossfade curve, mixing and the album rule.
 * argv[1]: the folder with the fixtures. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gapless.h"
#include "mp4.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

/* frames from the Xing header of the frame at b */
static uint32_t xing_frames(const uint8_t *b)
{
    uint32_t h = be32(b);
    int ver = (h >> 19) & 3, mono = ((h >> 6) & 3) == 3;
    int off = ver == 3 ? (mono ? 21 : 36) : (mono ? 13 : 21);
    return (be32(b + off + 4) & 1) ? be32(b + off + 8) : 0;
}

/* An MP3 of `music` frames per channel: the padding must leave exactly that. */
static void mp3_case(const char *dir, const char *name, long music, int spf)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    CHECK(f, "%s missing", path);
    if (!f) return;
    uint8_t b[1024];
    size_t n = fread(b, 1, sizeof(b), f);
    fclose(f);
    size_t at = 0;
    if (n >= 10 && !memcmp(b, "ID3", 3))
        at = 10 + ((b[6] & 0x7F) << 21 | (b[7] & 0x7F) << 14 | (b[8] & 0x7F) << 7 | (b[9] & 0x7F));
    CHECK(at < n, "%s: ID3 tag too big for the test", name);
    GapInfo g = {0, 0};
    int ok = gap_lame(b + at, (int)(n - at), &g);
    CHECK(ok, "%s: no LAME tag found", name);
    long total = (long)xing_frames(b + at) * spf;
    printf("  %s: delay %d pad %d, %ld decoded -> %ld (want %ld)\n", name, g.delay, g.pad, total,
           total - g.delay - g.pad, music);
    CHECK(total - g.delay - g.pad == music, "%s: %ld - %d - %d != %ld", name, total, g.delay, g.pad, music);
}

static void m4a_case(const char *dir, const char *name, long music)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    Mp4 m;
    const char *why = "";
    int r = mp4_open_audio(&m, path, &why);
    CHECK(r == 0, "%s: %s", name, why);
    if (r) return;
    GapInfo g = {0, 0};
    uint64_t frames = (uint64_t)m.audio.count * 1024;
    r = gap_from_edit(m.audio_edit_skip, m.audio_edit_len, m.audio.timescale, m.sample_rate, frames, &g);
    printf("  %s: edit skip %lld len %llu (ts %u): delay %d pad %d, %llu decoded -> %lld (want %ld)\n", name,
           (long long)m.audio_edit_skip, (unsigned long long)m.audio_edit_len, m.audio.timescale, g.delay, g.pad,
           (unsigned long long)frames, (long long)frames - g.delay - g.pad, music);
    CHECK(r == 0, "%s: no padding from the edit list", name);
    CHECK((long long)frames - g.delay - g.pad == music, "%s: wrong length", name);
    mp4_close(&m);
}

static void test_smpb(void)
{
    int d = 0, p = 0;
    uint64_t t = 0;
    CHECK(gap_itunsmpb(" 00000000 00000840 000001CC 0000000000A1B2C3 00000000 00000000", &d, &p, &t) == 0, "smpb");
    CHECK(d == 0x840 && p == 0x1CC && t == 0xA1B2C3, "smpb values %d %d %llu", d, p, (unsigned long long)t);
    CHECK(gap_itunsmpb("00000000 00000000 00000000 0000000000000000", &d, &p, &t) != 0, "all zero");
    CHECK(gap_itunsmpb(" 00000000 0000084Q 000001CC 0000000000A1B2C3", &d, &p, &t) != 0, "bad digit");
    CHECK(gap_itunsmpb(" 00000000 00000840", &d, &p, &t) != 0, "too short");
    CHECK(gap_itunsmpb("", &d, &p, &t) != 0, "empty");
}

static void test_edit(void)
{
    GapInfo g;
    CHECK(gap_from_edit(2112, 44100, 44100, 44100, 2112 + 44100 + 300, &g) == 0 && g.delay == 2112 && g.pad == 300, "edit");
    CHECK(gap_from_edit(-1, 0, 44100, 44100, 1024, &g) != 0, "no edit");
    CHECK(gap_from_edit(0, 0, 44100, 44100, 1024, &g) != 0, "nothing to cut");
    CHECK(gap_from_edit(1024, 0, 44100, 44100, 9999, &g) == 0 && g.delay == 1024 && g.pad == 0, "skip only");
    /* timescale differing from the rate */
    CHECK(gap_from_edit(2048, 0, 88200, 44100, 9999, &g) == 0 && g.delay == 1024, "timescale");
}

static void test_resampler(void)
{
    /* the same rate: the input again, two frames late */
    Resampler r;
    rs_init(&r, 44100, 44100);
    int16_t in[2000], out[2000];
    for (int i = 0; i < 1000; i++) in[i * 2] = (int16_t)(i * 7), in[i * 2 + 1] = (int16_t)(-i * 5);
    int used, made = rs_run(&r, in, 1000, out, 1000, &used);
    CHECK(used == 1000 && made >= 997, "identity made %d used %d", made, used);
    int same = 1;
    for (int i = 2; i < made; i++)
        if (out[i * 2] != in[(i - 2) * 2] || out[i * 2 + 1] != in[(i - 2) * 2 + 1]) same = 0;
    CHECK(same, "identity changes the sound");

    /* 48 kHz -> 44.1: a 1 kHz sine keeps its pitch and loudness, fed in odd pieces */
    rs_init(&r, 48000, 44100);
    enum { N = 48000 };
    static int16_t src[N * 2], dst[N * 2];
    for (int i = 0; i < N; i++)
        src[i * 2] = src[i * 2 + 1] = (int16_t)lrint(20000 * sin(2 * M_PI * 1000 * i / 48000.0));
    int at = 0, got = 0;
    while (at < N && got < N) {
        int piece = 1 + (at * 7919) % 613;
        if (piece > N - at) piece = N - at;
        int u;
        got += rs_run(&r, src + at * 2, piece, dst + got * 2, N - got, &u);
        CHECK(u == piece || got == N, "piece not used up");
        at += u;
    }
    CHECK(abs(got - 44100) <= 2, "48k -> 44.1k made %d frames", got);
    int cross = 0, peak = 0;
    for (int i = 1; i < got; i++) {
        if ((dst[(i - 1) * 2] < 0) != (dst[i * 2] < 0)) cross++;
        if (abs(dst[i * 2]) > peak) peak = abs(dst[i * 2]);
    }
    CHECK(abs(cross - 2000) <= 2, "pitch: %d zero crossings in a second", cross);
    CHECK(peak > 19800 && peak < 20200, "loudness: peak %d", peak);
    CHECK(rs_need(&r, 100) >= 100 * 48000 / 44100, "need");

    /* 32 kHz up to 48: the length */
    rs_init(&r, 32000, 48000);
    int u;
    got = rs_run(&r, src, 3200, dst, N, &u);
    CHECK(u == 3200 && abs(got - 4800) <= 3, "32k -> 48k made %d", got);
}

static void test_mix(void)
{
    CHECK(xf_gain(0, 1000) == 0 && xf_gain(1000, 1000) == Q15 && xf_gain(5000, 1000) == Q15, "ends");
    int last = -1, power_ok = 1;
    for (int p = 0; p <= 1000; p += 7) {
        int gi = xf_gain(p, 1000), go = xf_gain(1000 - p, 1000);
        if (gi < last) last = 1 << 30;
        else last = gi;
        double pw = ((double)gi * gi + (double)go * go) / ((double)Q15 * Q15);
        if (pw < 0.98 || pw > 1.02) power_ok = 0;
    }
    CHECK(last != 1 << 30, "fade-in not rising");
    CHECK(power_ok, "not equal power");
    CHECK(abs(xf_gain(500, 1000) - 23170) < 40, "middle %d", xf_gain(500, 1000));

    int32_t acc[8] = {0};
    int16_t a[8] = {30000, -30000, 1000, -1000, 16384, 16384, 0, 0}, out[8];
    mix_add(acc, a, 4, Q15, Q15);
    mix_add(acc, a, 4, Q15, Q15);
    mix_out(out, acc, 4);
    CHECK(out[0] == 32767 && out[1] == -32768 && out[2] == 2000 && out[3] == -2000, "clipping");
    memset(acc, 0, sizeof(acc));
    mix_add(acc, a, 4, Q15 / 2, Q15 / 2);
    CHECK(acc[0] == 15000 && acc[3] == -500, "half gain");
    memset(acc, 0, sizeof(acc));
    mix_add(acc, a, 4, 0, Q15); /* a ramp: the first frame silent, then rising */
    CHECK(acc[0] == 0 && acc[2] > 0 && acc[2] < 1000 && acc[4] > acc[2] * 8, "ramp %d %d %d", acc[0], acc[2], acc[4]);
}

static void test_album(void)
{
    GapSong a = {"ms0:/MUSIC/Album/01.mp3", "Album", 1, 4}, b = {"ms0:/MUSIC/Album/02.mp3", "Album", 2, 5};
    CHECK(gap_same_album(&a, &b), "next track");
    b.track = 3;
    CHECK(!gap_same_album(&a, &b), "a track skipped");
    b.track = 0;
    CHECK(gap_same_album(&a, &b), "no numbers: next in the list");
    b.index = 9;
    CHECK(!gap_same_album(&a, &b), "no numbers, not next in the list");
    b.album = "Other";
    b.track = 2;
    CHECK(!gap_same_album(&a, &b), "another album");
    GapSong c = {"ms0:/MUSIC/X/a.mp3", "", 0, 0}, d = {"ms0:/MUSIC/X/b.mp3", "", 0, 1}, e = {"ms0:/MUSIC/Y/b.mp3", "", 0, 1};
    CHECK(!gap_same_album(&c, &d), "untagged songs in order are singles: they fade");
    CHECK(!gap_same_album(&c, &e), "untagged, another folder");
    d.index = 1;
    d.album = "Album";
    CHECK(!gap_same_album(&c, &d), "one tagged, one not");

    CHECK(gap_fade_ms(0, 0, 0, 60000, 60000) == 0, "off");
    CHECK(gap_fade_ms(6, 0, 0, 60000, 60000) == 6000, "6 s");
    CHECK(gap_fade_ms(6, 1, 0, 60000, 60000) == 0, "repeat one");
    CHECK(gap_fade_ms(6, 0, 1, 60000, 60000) == 0, "album");
    CHECK(gap_fade_ms(10, 0, 0, 3000, 60000) == 3000, "little left");
    CHECK(gap_fade_ms(10, 0, 0, 60000, 8000) == 4000, "short next song");
    CHECK(gap_fade_ms(10, 0, 0, 60000, 0) == 10000, "next length unknown");
    CHECK(gap_fade_ms(10, 0, 0, 150, 60000) == 0, "too short to fade");
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/gapless";
    /* lengths as tests/run.sh made them */
    mp3_case(dir, "lame44.mp3", 44100 + 777, 1152);
    mp3_case(dir, "lame48v.mp3", 48000 * 2 + 31, 1152);
    mp3_case(dir, "lame22mono.mp3", 22050 + 5, 576);
    mp3_case(dir, "lametag.mp3", 44100 + 777, 1152);
    { /* 32 kbps at 22 kHz: frames too small for LAME's tag, so there is none (and nothing is cut) */
        char path[512];
        uint8_t b[512];
        GapInfo g = {0, 0};
        snprintf(path, sizeof(path), "%s/lame22tiny.mp3", dir);
        FILE *f = fopen(path, "rb");
        size_t n = f ? fread(b, 1, sizeof(b), f) : 0;
        if (f) fclose(f);
        CHECK(n > 100 && !gap_lame(b, (int)n, &g), "lame22tiny.mp3: a tag where there is none");
    }
    mp3_case(dir, "ffmpeg44.mp3", 44100 + 777, 1152);
    m4a_case(dir, "ffmpeg44.m4a", 44100 + 777);
    m4a_case(dir, "ffmpeg48.m4a", 48000 * 2 + 31);
    test_smpb();
    test_edit();
    test_resampler();
    test_mix();
    test_album();
    if (fails) {
        printf("test_gapless: %d FAILED\n", fails);
        return 1;
    }
    printf("test_gapless: ok\n");
    return 0;
}
