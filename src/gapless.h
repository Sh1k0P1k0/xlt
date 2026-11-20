/* XLT - gapless playback and crossfades: the parts that don't touch the PSP
 * (host-tested in tests/test_gapless.c). music.c does the decoding and output.
 *
 *  - Encoder padding: MP3 and AAC encoders add silence at the start (the
 *    encoder's and decoder's delay) and the end (to fill the last frame). LAME's
 *    tag (also written by ffmpeg) and iTunes' iTunSMPB say how much; MP4's edit
 *    list does too. Dropping it is what makes one song run into the next.
 *  - A resampler, for when the next song's rate isn't the output's: the output
 *    channel can't change rate without a gap, so the new song is converted until
 *    the next natural break (a skip, a seek, a new list).
 *  - Mixing with gains in Q15 (1.0 = 32768) and equal-power crossfade curves.
 *  - The album rule: songs that follow on from each other on one album are joined
 *    without a fade even when crossfade is on.
 */
#ifndef GAPLESS_H
#define GAPLESS_H

#include <stdint.h>

#define Q15 32768

/* Frames (samples per channel) to drop at the start and to hold back at the end. */
typedef struct {
    int delay, pad;
} GapInfo;

/* MP3: b holds the first frame (n bytes from its header). 1 if it is a Xing/Info
 * frame with a LAME-style tag (LAME, or ffmpeg's Lavc/Lavf); g then has the start
 * to drop (the decoder's 529 included) and the end to hold back. 0 otherwise. */
int gap_lame(const uint8_t *b, int n, GapInfo *g);

/* iTunSMPB, " 00000000 00000840 000001CC 0000000000A1B2C3 ...": 0 if it parses
 * (delay and padding in frames, total frames of music, 0 if not given). */
int gap_itunsmpb(const char *v, int *delay, int *pad, uint64_t *total);

/* MP4 edit list of an AAC track: skip = media time of its first edit, len = that
 * edit's length (both in the track's timescale, len 0 if unknown). frames = samples
 * in the track x 1024. Fills g at `rate` (track timescale `ts`). 0 if it gives padding. */
int gap_from_edit(int64_t skip, uint64_t len, uint32_t ts, int rate, uint64_t frames, GapInfo *g);

/* Catmull-Rom resampler, 16-bit stereo. */
typedef struct {
    uint32_t step;      /* input frames per output frame, Q16 */
    uint32_t phase;     /* Q16, between h[1] and h[2] */
    int16_t h[4][2];
    int primed;
} Resampler;

void rs_init(Resampler *r, int in_rate, int out_rate);
/* Makes up to nout frames from in (nin frames); returns the frames made and puts
 * the frames of in it used up in *used. */
int rs_run(Resampler *r, const int16_t *in, int nin, int16_t *out, int nout, int *used);
/* Input frames needed for nout more output frames (an upper bound). */
int rs_need(const Resampler *r, int nout);

/* The fade-in gain (Q15) at pos of len frames, equal power: sin(pi/2 * pos/len).
 * The fade-out gain at pos is xf_gain(len - pos, len). */
int xf_gain(int pos, int len);

/* acc[2n] += src[2n] * gain, the gain going from g0 to g1 over the n frames (Q15). */
void mix_add(int32_t *acc, const int16_t *src, int n, int g0, int g1);
/* dst = acc, clipped to 16 bits. */
void mix_out(int16_t *dst, const int32_t *acc, int n);

/* What the album rule needs to know of a song. */
typedef struct {
    const char *path, *album; /* album "" if unknown */
    int track;                /* track number, 0 if unknown */
    int index;                /* its place in the list as given (not the shuffled order) */
} GapSong;

/* 1 if b follows a on one album, so they're joined without a fade: the same album
 * tag and the next track number (or the next song in the list when the numbers
 * aren't known). Songs without an album tag always fade. */
int gap_same_album(const GapSong *a, const GapSong *b);

/* The crossfade length in ms from song a to b: 0 = join them gapless. fade_s is the
 * setting (0 = off), a_left_ms what is left of a, b_ms b's length (0 if unknown). */
int gap_fade_ms(int fade_s, int repeat_one, int same_album, int a_left_ms, int b_ms);

#endif
