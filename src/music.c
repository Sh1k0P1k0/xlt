/* XLT - MP3 playback.
 *
 * Same technique as psp5's player, which is tested on hardware:
 *  - set a compiled SDK version first, or sceMp3 only accepts 44.1 kHz;
 *  - load AVCODEC + MP3 with sceUtilityLoadModule ("already loaded" is fine);
 *  - feed the decoder from the file, play through the SRC channel (any rate),
 *    mono frames expanded to stereo;
 *  - just after a seek the decoder may return MP3_END for frames that lack
 *    data from the frame before (bit reservoir): that is not the end;
 *  - reopen the file if a read fails after sleep.
 * The player thread runs above the UI (0x14 < 0x20) and spends its time in
 * sceAudioSRCOutputBlocking, so the UI never makes the audio stutter.
 *
 * The UI thread owns the playlist copy. It only replaces it after the player
 * has acknowledged a stop, so the player never reads a list being freed.
 *
 * Songs play in s_order (0..n-1, or shuffled with the chosen one first);
 * the player thread owns the order's contents once a list is playing and
 * mixes or unmixes it when shuffle is switched (CMD_SHUFFLE).
 *
 * Where playback is (list, song, time, shuffle, repeat) is saved in cache/
 * (resume.c) on every song change, pause and every 20 s, and when the player
 * shuts down for a game, USB or exit, so music_resume() can carry on.
 *
 * AAC (.m4a, .aac) goes through the PSP's own decoder (sceAudiocodec, the way the
 * video player's sound does) and FLAC through flac.c on the CPU.
 *
 * Gapless playback and crossfades (gapless.c has the portable parts): a song is
 * a Dec, and there are two. While one plays, the loader thread opens the next
 * (PRELOAD_MS before it is needed), so the file, its tags and the decoder's start
 * never hold up the sound. Every song, whatever its format, is read as 16-bit
 * stereo frames with the encoder's padding left out (LAME tag, iTunSMPB or the MP4
 * edit list), and the output is one stream of blocks of s_chunk frames (4096 if
 * the hardware takes it, ~85 ms): where a song ends mid-block, the next one fills
 * the rest. With Settings > Sound > Crossfade on, the next song starts that long
 * before the end and the two are mixed with equal-power curves; songs that follow
 * on from each other on one album are joined without a fade. The output channel
 * keeps its rate across songs (changing it makes a gap): a song at another rate
 * is resampled until the next break (a skip, a seek, a new list), which opens the
 * channel at its own rate again. Volume levelling is a gain on each song's frames
 * (so two songs can be mixed); the sleep timer is the channel's volume.
 *
 * The equalizer (eq.c, Settings > Sound or START in Now Playing) runs on every
 * mixed block just before it is handed over, in the player thread: the UI only
 * changes g_cfg's preset and gains, and eq_process notices.
 */
/* Parts of this file (the MP3 engine (decoder setup, stream feeding, frame and VBR parsing, seeking, SRC output)) are adapted from psp5 by 08Weber,
 * https://github.com/08Weber/psp5, GPL-3.0. */
#include <pspkernel.h>
#include <pspaudio.h>
#include <pspmp3.h>
#include <pspaudiocodec.h>
#include <psputility.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "music.h"
#include "library.h"
#include "system.h"
#include "video.h"
#include "resume.h"
#include "bg.h"
#include "ui.h"
#include "audiofmt.h"
#include "gamecfg.h"
#include "flac.h"
#include "mp4.h"
#include "readwin.h"
#include "eq.h"
#include "gapless.h"

#define MP3_END 0x80671402
#define ALREADY_LOADED 0x80020139
#define ALREADY_LOADED2 0x80111102
#define MAX_TRACKS 1000
#define MAX_SAMPLES 1152
#define QUEUE 16

typedef struct {
    char path[sizeof(((Entry *)0)->path)];
    char title[128];
    char artist[48];
    char album[48];   /* for the crossfade's album rule; "" if unknown */
    int tracknum;     /* 0 if unknown */
} Track;

enum { CMD_PLAY, CMD_TOGGLE, CMD_NEXT, CMD_PREV, CMD_SEEK, CMD_STOP, CMD_SHUFFLE, CMD_QUIT };

/* UI thread */
static Track *s_list;
static int s_count;
static int s_ready;        /* modules loaded, resource initialised */
static int s_loaded_avcodec, s_loaded_mp3;
static SceUID s_thread = -1;
static int s_seq;          /* commands posted */
static int *s_order;       /* play order: positions -> tracks */
static int s_shuffle;      /* the switch (the player's copy follows by CMD_SHUFFLE) */
static unsigned s_seed;
static char s_cache[200];  /* <app>/cache, where the place is saved; "" before music_init */
static ResumeState s_saved; /* what music.txt says */

/* shared */
static volatile int s_q_cmd[QUEUE], s_q_arg[QUEUE], s_q_seq[QUEUE];
static volatile int s_q_head, s_q_tail;
static volatile int s_done_seq; /* last command the player finished */
static volatile int s_state = MUSIC_STOPPED;
static volatile int s_current = -1;  /* track index in s_list */
static volatile int s_pos = -1;      /* its position in s_order */
static volatile int s_repeat;        /* REPEAT_* (read by the player) */
static volatile int s_ended;         /* the list played to its end: nothing to resume */
static volatile int s_volume = PSP_AUDIO_VOLUME_MAX; /* lowered as the sleep timer runs out */
static u32 s_sleep_at;               /* now_ms() when the sleep timer stops the music, 0 = off */
static Eq s_eq;                      /* the equalizer's filters, on the mixed output (player thread) */
static int s_sleep_min;
static volatile int s_elapsed_ms, s_duration_ms;
static volatile int s_gen;
static const char *volatile s_open_err; /* why a song was skipped, for the UI thread */

static u32 be32(const unsigned char *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static void bump(void) { s_gen++; }

/* The gains the equalizer is set to; 0 if it would do nothing. */
static int eq_wanted(int g[EQ_BANDS])
{
    eq_gains(g_cfg.eq_preset, g_cfg.eq_custom, g);
    for (int b = 0; b < EQ_BANDS; b++)
        if (g[b])
            return 1;
    s_eq.rate = 0; /* made afresh, memory and all, when it is turned on again */
    return 0;
}

/* ------------------------------------------------------------- decoders */

#define ALT_MAX 4096          /* frames per output block, if the hardware takes that many */
#define AAC_SAMPLES 1024      /* one AAC frame */
#define AAC_WINDOW (64 * 1024) /* a read every couple of seconds, never one per frame */
#define AAC_FRAME_MAX (8 * 1024)
#define ADTS_LOOK 4096
#define STAGE_MAX 8192        /* frames decoded and not yet played: one MP3/AAC frame + the end held back */
#define HOLD_MAX (STAGE_MAX - MAX_SAMPLES)
#define RS_IN 1024            /* frames read at a time for the resampler */
#define OUT_BUFS 4            /* the hardware plays one while the next is made */
#define PRELOAD_MS 10000      /* the next song is opened this long before it is needed */

typedef struct {
    unsigned long codec[65] __attribute__((aligned(64)));
    short pcm[AAC_SAMPLES * 2] __attribute__((aligned(64))); /* the decoder writes RAM directly */
    uint8_t in[AAC_FRAME_MAX] __attribute__((aligned(64)));
    Mp4 m4a;
    int has_m4a, errors;
    uint32_t sample;           /* the next one (M4A) */
    uint64_t adts_pos, adts_start;
    ReadWin rw;
    uint8_t *win;
    void *work;
} Aac;

/* One song being decoded. There are two: the playing one and the next (opened
 * ahead by the loader thread), or during a crossfade the old and the new. */
typedef struct {
    int slot;
    int track, pos;            /* in s_list and s_order; track -1 = closed */
    SceUID fd;
    u32 size, start;           /* file size, first byte after the ID3v2 tag */
    int format;                /* AUDIO_* */
    int rate, channels, duration_ms;
    /* MP3 */
    SceInt32 handle;
    int eof, spf, seek_grace;
    u32 first_hdr, audio_at;   /* the first frame's header; where the music starts (after a Xing/Info frame) */
    u32 audio_end;             /* where it ends: before an ID3v1 or APE tag (the decoder makes a frame of those) */
    uint64_t xing_total;       /* frames of sound the Xing/Info header counts, 0 if unknown */
    unsigned char toc[100];
    int has_toc;
    /* AAC, FLAC */
    Aac *aac;
    Flac flac;
    int16_t *fpcm;             /* FLAC's decoded block */
    int fhave, fat;
    /* decoded, not yet played (MP3, AAC): 16-bit stereo frames */
    int16_t *stage;
    int have, at;
    int skip, skip0, hold;     /* encoder padding: still to drop at the start (skip0 from the start); kept back at the end */
    const char *gap_from;      /* where the padding figures came from, for the log */
    int ended, failed;         /* nothing more to decode; it stopped on an error */
    /* time */
    int base_ms;               /* where the last start or seek put it */
    uint64_t taken;            /* frames given out since then */
    uint64_t decoded;          /* frames out of the decoder since the start (for the log) */
    u32 from_byte;             /* MP3: where the decoder was last started (open or seek), for the log */
    uint64_t since;            /* frames out of the decoder since then */
    u32 fmax, rmax, rtime;     /* the slowest FLAC block and read, in microseconds; all the reading */
    /* volume levelling: the tags, and the gain they give (worked out again if Settings changes the mode) */
    Id3Info tags;
    int level_mode, level;     /* level in Q15, only ever a cut */
    /* to the output's rate, if the song's isn't it */
    Resampler rs;
    int rs_on, rs_have, rs_at;
    int16_t *rsbuf;
} Dec;

/* psp5's tested sizes: the decoder double-buffers inside the PCM buffer */
static unsigned char s_mp3buf[2][32 * 1024] __attribute__((aligned(64)));
static unsigned char s_pcmbuf[2][1152 * 2 * 2 * 2] __attribute__((aligned(64)));
static int16_t s_stage[2][STAGE_MAX * 2] __attribute__((aligned(64)));
static int16_t s_rsbuf[2][RS_IN * 2] __attribute__((aligned(64)));
static Dec s_dec[2] = {{.slot = 0, .track = -1, .fd = -1, .handle = -1}, {.slot = 1, .track = -1, .fd = -1, .handle = -1}};

/* Reads for the alt decoders; a file closed by sleep is opened again. */
static int alt_read(void *ctx, uint64_t off, void *dst, uint32_t len)
{
    Dec *d = ctx;
    int got = -1;
    u32 t0 = sceKernelGetSystemTimeLow();
    if (sceIoLseek(d->fd, (SceOff)off, PSP_SEEK_SET) >= 0)
        got = sceIoRead(d->fd, dst, len);
    if (got < 0 && d->track >= 0) {
        sceIoClose(d->fd);
        d->fd = sceIoOpen(s_list[d->track].path, PSP_O_RDONLY, 0777);
        if (d->fd >= 0 && sceIoLseek(d->fd, (SceOff)off, PSP_SEEK_SET) >= 0)
            got = sceIoRead(d->fd, dst, len);
    }
    u32 t = sceKernelGetSystemTimeLow() - t0;
    d->rtime += t;
    if (t > d->rmax) d->rmax = t;
    return got;
}

static void dec_close(Dec *d)
{
    if (d->track >= 0) {
        video_note("song %d (%s, %d Hz): %llu frames decoded, %llu played; padding %d + %d (%s); slowest read %u ms%s",
                   d->track, audio_format_name(d->format), d->rate, (unsigned long long)d->decoded,
                   (unsigned long long)d->taken, d->skip0, d->hold, d->gap_from, d->rmax / 1000,
                   d->failed ? "; stopped on an error" : "");
        if (d->format == AUDIO_MP3 && d->ended && !d->failed) /* to check against the file: did every frame come out? */
            video_note("song %d reached its end: %llu frames out of the decoder from byte %u (music %u to %u, file %u bytes; header counts %llu)",
                       d->track, (unsigned long long)d->since, d->from_byte, d->audio_at, d->audio_end, d->size,
                       (unsigned long long)d->xing_total);
    }
    if (d->format == AUDIO_FLAC && d->fpcm) {
        flac_close(&d->flac);
        free(d->fpcm);
        d->fpcm = NULL;
    }
    if (d->aac) {
        if (d->aac->has_m4a)
            mp4_close(&d->aac->m4a);
        free(d->aac->win);
        free(d->aac->work);
        free(d->aac);
        d->aac = NULL;
    }
    if (d->handle >= 0) {
        sceMp3ReleaseMp3Handle(d->handle);
        d->handle = -1;
    }
    if (d->fd >= 0) {
        sceIoClose(d->fd);
        d->fd = -1;
    }
    d->track = d->pos = -1;
}

static int open_flac(Dec *d)
{
    int r = flac_open(&d->flac, alt_read, d, d->size);
    if (r != 0) {
        video_note("flac: open = %d", r);
        s_open_err = r == FLAC_ERR_RATE ? "its sample rate is too unusual" : "it isn't a FLAC file";
        return -1;
    }
    d->fpcm = malloc((size_t)d->flac.alloc_block * 4);
    if (!d->fpcm) {
        flac_close(&d->flac);
        return -1;
    }
    d->rate = d->flac.out_rate;
    d->channels = 2;
    d->duration_ms = flac_duration_ms(&d->flac);
    video_note("flac: %d Hz, %d bit, %d ch, blocks up to %d", d->flac.rate, d->flac.bps, d->flac.channels, d->flac.max_block);
    return 0;
}

/* Finds the first frame and works out the length from the frames that follow. */
static int open_adts(Dec *d, int *object, int *rate)
{
    Aac *a = d->aac;
    uint32_t avail = d->size - d->start, look = avail < ADTS_LOOK ? avail : ADTS_LOOK;
    if (look < 7 || readwin_get(&a->rw, d->start, look, a->in) != 0)
        return -1;
    int at = adts_find(a->in, (int)look);
    Adts h;
    if (at < 0 || adts_parse(a->in + at, (int)look - at, &h) != 0)
        return -1;
    a->adts_start = a->adts_pos = (uint64_t)d->start + (uint32_t)at;
    *object = h.object;
    *rate = h.rate;
    uint64_t pos = a->adts_start, bytes = 0;
    int frames = 0;
    uint8_t b[9];
    while (frames < 256 && readwin_get(&a->rw, pos, 9, b) == 0 && adts_parse(b, 9, &h) == 0) {
        pos += (uint32_t)h.frame;
        bytes += (uint32_t)h.frame;
        frames++;
    }
    if (frames && bytes && *rate)
        d->duration_ms = (int)(((uint64_t)d->size - a->adts_start) * frames / bytes * 1024 * 1000 / (uint32_t)*rate);
    return 0;
}

static int open_aac(Dec *d)
{
    Aac *a = memalign(64, sizeof(*a));
    if (!a)
        return -1;
    memset(a, 0, sizeof(*a));
    d->aac = a;
    a->win = memalign(64, AAC_WINDOW);
    if (!a->win)
        return -1;
    readwin_init(&a->rw, a->win, AAC_WINDOW, alt_read, d);
    int object = 0, rate = 0;
    if (d->format == AUDIO_M4A) {
        const char *why = "";
        if (mp4_open_audio(&a->m4a, s_list[d->track].path, &why) != 0) {
            video_note("m4a: %s", why);
            s_open_err = why;
            return -1;
        }
        a->has_m4a = 1;
        object = a->m4a.aac_object;
        rate = a->m4a.sample_rate;
        d->duration_ms = a->m4a.duration_ms ? (int)a->m4a.duration_ms : (int)mp4_time_ms(&a->m4a.audio, a->m4a.audio.count);
    } else if (open_adts(d, &object, &rate) != 0) {
        s_open_err = "it isn't an AAC file";
        return -1;
    }
    if (object != 2 || !audio_rate_ok(rate)) {
        video_note("aac: object %d at %d Hz can't be played", object, rate);
        s_open_err = object != 2 ? "only plain AAC (LC) is supported" : "its sample rate is too unusual";
        return -1;
    }
    sceKernelDcacheWritebackInvalidateRange(a->pcm, sizeof(a->pcm));
    int r = sceAudiocodecCheckNeedMem(a->codec, PSP_CODEC_AAC);
    if (r < 0 || !a->codec[4] || !(a->work = memalign(64, a->codec[4]))) {
        video_note("aac: CheckNeedMem = %08X, %lu bytes", r, a->codec[4]);
        return -1;
    }
    a->codec[3] = (unsigned long)a->work;
    a->codec[10] = (unsigned long)rate;
    r = sceAudiocodecInit(a->codec, PSP_CODEC_AAC);
    if (r < 0) {
        video_note("aac: Init(%d Hz) = %08X", rate, r);
        s_open_err = "the AAC decoder didn't start";
        return -1;
    }
    d->rate = rate;
    d->channels = 2;
    /* the padding: iTunes' tag, else the edit list (ffmpeg and most others) */
    GapInfo g;
    if (d->tags.has_smpb) {
        d->skip0 = d->tags.smpb_delay;
        d->hold = d->tags.smpb_pad;
        d->gap_from = "iTunSMPB";
    } else if (a->has_m4a && gap_from_edit(a->m4a.audio_edit_skip, a->m4a.audio_edit_len, a->m4a.audio.timescale, rate,
                                           (uint64_t)a->m4a.audio.count * AAC_SAMPLES, &g) == 0) {
        d->skip0 = g.delay;
        d->hold = g.pad;
        d->gap_from = "edit list";
    }
    return 0;
}

/* The next frame into a->in. Its size, 0 at the end of the song, -1 if it can't be read. */
static int aac_frame(Dec *d)
{
    Aac *a = d->aac;
    uint32_t size;
    if (a->has_m4a) {
        if (a->sample >= a->m4a.audio.count)
            return 0;
        size = mp4_size(&a->m4a.audio, a->sample);
        if (!size || size > AAC_FRAME_MAX || readwin_get(&a->rw, mp4_offset(&a->m4a.audio, a->sample), size, a->in) != 0)
            return -1;
        a->sample++;
    } else {
        uint8_t b[9];
        Adts h;
        if (a->adts_pos + 7 >= d->size || readwin_get(&a->rw, a->adts_pos, 9, b) != 0)
            return 0;
        if (adts_parse(b, 9, &h) != 0)
            return -1;
        size = (uint32_t)(h.frame - h.header);
        if (size > AAC_FRAME_MAX || a->adts_pos + (uint32_t)h.frame > d->size ||
            readwin_get(&a->rw, a->adts_pos + (uint32_t)h.header, size, a->in) != 0)
            return 0; /* cut off: the end, as far as anyone can tell */
        a->adts_pos += (uint32_t)h.frame;
    }
    sceKernelDcacheWritebackRange(a->in, size); /* the decoder reads RAM directly */
    return (int)size;
}

static u32 id3v2_size(SceUID fd)
{
    unsigned char h[10];
    sceIoLseek32(fd, 0, PSP_SEEK_SET);
    if (sceIoRead(fd, h, 10) != 10 || memcmp(h, "ID3", 3))
        return 0;
    u32 n = (h[6] & 0x7F) << 21 | (h[7] & 0x7F) << 14 | (h[8] & 0x7F) << 7 | (h[9] & 0x7F);
    return 10 + n + ((h[5] & 0x10) ? 10 : 0);
}

static int feed(Dec *d)
{
    unsigned char *dst;
    SceInt32 towrite, srcpos;
    if (d->eof)
        return 0;
    if (sceMp3GetInfoToAddStreamData(d->handle, &dst, &towrite, &srcpos) < 0)
        return -1;
    if (towrite <= 0)
        return 0;
    int got = -1;
    u32 t0 = sceKernelGetSystemTimeLow();
    if (sceIoLseek32(d->fd, srcpos, PSP_SEEK_SET) >= 0)
        got = sceIoRead(d->fd, dst, towrite);
    if (got < 0 && d->track >= 0) { /* the file is closed by sleep: reopen once */
        sceIoClose(d->fd);
        d->fd = sceIoOpen(s_list[d->track].path, PSP_O_RDONLY, 0777);
        if (d->fd >= 0 && sceIoLseek32(d->fd, srcpos, PSP_SEEK_SET) >= 0)
            got = sceIoRead(d->fd, dst, towrite);
    }
    u32 t = sceKernelGetSystemTimeLow() - t0;
    if (t > d->rmax) d->rmax = t;
    if (got < 0)
        return -1;
    if (got == 0) {
        d->eof = 1;
        return 0;
    }
    return sceMp3NotifyAddStreamData(d->handle, got) < 0 ? -1 : 0;
}

static const int MP3_RATES[4][3] = {{11025, 12000, 8000}, {0, 0, 0}, {22050, 24000, 16000}, {44100, 48000, 32000}};

static int frame_len(u32 h)
{
    static const short kbps1[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static const short kbps2[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    int ver = (h >> 19) & 3, layer = (h >> 17) & 3, bi = (h >> 12) & 15, ri = (h >> 10) & 3;
    if ((h & 0xFFE00000) != 0xFFE00000 || ver == 1 || layer != 1 || ri == 3)
        return 0;
    int kbps = ver == 3 ? kbps1[bi] : kbps2[bi], rate = MP3_RATES[ver][ri];
    if (!kbps)
        return 0;
    return (ver == 3 ? 144 : 72) * kbps * 1000 / rate + ((h >> 9) & 1);
}

/* Duration in ms from a Xing/Info or VBRI header (0 if absent); keeps the TOC for
 * seeking, and LAME's padding. Such a frame holds no sound: the music starts after it. */
static int parse_first_frame(Dec *d)
{
    unsigned char b[256];
    d->first_hdr = 0;
    d->has_toc = 0;
    d->audio_at = d->start;
    sceIoLseek32(d->fd, d->start, PSP_SEEK_SET);
    if (sceIoRead(d->fd, b, sizeof(b)) != sizeof(b))
        return 0;
    u32 h = be32(b);
    int len = frame_len(h);
    if (!len)
        return 0;
    d->first_hdr = h;
    int ver = (h >> 19) & 3, ri = (h >> 10) & 3, mono = ((h >> 6) & 3) == 3;
    int rate = MP3_RATES[ver][ri], spf = ver == 3 ? 1152 : 576;
    d->spf = spf;
    int off = ver == 3 ? (mono ? 21 : 36) : (mono ? 13 : 21);
    u32 frames = 0;
    if (!memcmp(b + off, "Xing", 4) || !memcmp(b + off, "Info", 4)) {
        u32 flags = be32(b + off + 4);
        int p = off + 8;
        if (flags & 1) { frames = be32(b + p); p += 4; }
        if (flags & 2) p += 4;
        d->xing_total = (uint64_t)frames * (uint32_t)spf;
        if ((flags & 4) && p + 100 <= (int)sizeof(b)) {
            memcpy(d->toc, b + p, 100);
            d->has_toc = 1;
        }
        d->audio_at = d->start + (u32)len;
        GapInfo g;
        if (gap_lame(b, len < (int)sizeof(b) ? len : (int)sizeof(b), &g)) {
            d->skip0 = g.delay;
            d->hold = g.pad;
            d->gap_from = "LAME tag";
        }
    } else if (!memcmp(b + 36, "VBRI", 4)) {
        frames = be32(b + 36 + 14);
        d->audio_at = d->start + (u32)len;
    }
    if (!d->gap_from[0] && d->tags.has_smpb) { /* an iTunes MP3 */
        d->skip0 = d->tags.smpb_delay;
        d->hold = d->tags.smpb_pad;
        d->gap_from = "iTunSMPB";
    }
    return frames && rate ? (int)((unsigned long long)frames * spf * 1000 / rate) : 0;
}

static int start_decoder(Dec *d, u32 offset)
{
    if (d->handle >= 0) {
        sceMp3ReleaseMp3Handle(d->handle);
        d->handle = -1;
    }
    SceMp3InitArg args;
    memset(&args, 0, sizeof(args));
    args.mp3StreamStart = offset;
    args.mp3StreamEnd = d->audio_end;
    args.mp3Buf = s_mp3buf[d->slot];
    args.mp3BufSize = sizeof(s_mp3buf[0]);
    args.pcmBuf = s_pcmbuf[d->slot];
    args.pcmBufSize = sizeof(s_pcmbuf[0]);
    d->handle = sceMp3ReserveMp3Handle(&args);
    if (d->handle < 0)
        return -1;
    d->eof = 0;
    d->from_byte = offset;
    d->since = 0;
    if (feed(d) < 0 || sceMp3Init(d->handle) < 0) {
        sceMp3ReleaseMp3Handle(d->handle);
        d->handle = -1;
        return -1;
    }
    sceMp3SetLoopNum(d->handle, 0);
    return 0;
}

/* Where the frames end: tags at the end of the file (ID3v1, APEv2) are left out,
 * or the PSP's decoder makes one more frame of them (seen on hardware). */
static u32 mp3_end(Dec *d)
{
    u32 end = d->size;
    unsigned char t[32];
    if (end >= 128 + d->start && sceIoLseek32(d->fd, end - 128, PSP_SEEK_SET) >= 0 && sceIoRead(d->fd, t, 3) == 3 &&
        !memcmp(t, "TAG", 3))
        end -= 128;
    if (end >= 32 + d->start && sceIoLseek32(d->fd, end - 32, PSP_SEEK_SET) >= 0 && sceIoRead(d->fd, t, 32) == 32 &&
        !memcmp(t, "APETAGEX", 8)) {
        u32 n = t[12] | t[13] << 8 | t[14] << 16 | (u32)t[15] << 24; /* the tag and this footer */
        if (t[23] & 0x80) n += 32;                                 /* and a header */
        if (n < end - d->start) end -= n;
    }
    return end;
}

static int open_mp3(Dec *d)
{
    d->audio_end = mp3_end(d);
    int dur = parse_first_frame(d);
    if (start_decoder(d, d->audio_at) < 0)
        return -1;
    d->rate = sceMp3GetSamplingRate(d->handle);
    d->channels = sceMp3GetMp3ChannelNum(d->handle);
    if (d->rate <= 0 || d->channels < 1 || d->channels > 2)
        return -1;
    if (dur <= 0) { /* CBR: from the bitrate */
        int kbps = sceMp3GetBitRate(d->handle);
        dur = kbps > 0 ? (int)((unsigned long long)(d->audio_end - d->start) * 8 / kbps) : 0;
    }
    d->duration_ms = dur;
    return 0;
}

/* Opens song `track` (at `pos` in the play order) in d. 0 if it will play.
 * Runs on the player thread or the loader thread. */
static int dec_open(Dec *d, int track, int pos)
{
    dec_close(d);
    if (track < 0 || track >= s_count)
        return -1;
    int slot = d->slot;
    memset(d, 0, sizeof(*d));
    d->slot = slot;
    d->handle = -1;
    d->track = track;
    d->pos = pos;
    d->stage = s_stage[slot];
    d->rsbuf = s_rsbuf[slot];
    d->gap_from = "";
    d->level_mode = -1;
    d->fd = sceIoOpen(s_list[track].path, PSP_O_RDONLY, 0777);
    if (d->fd < 0) {
        d->track = -1;
        return -1;
    }
    d->size = sceIoLseek32(d->fd, 0, PSP_SEEK_END);
    d->start = id3v2_size(d->fd);
    if (d->start >= d->size)
        d->start = 0;
    if (audio_tags(s_list[track].path, (uint64_t)d->size, &d->tags) != 0) /* loudness and gapless tags */
        memset(&d->tags, 0, sizeof(d->tags));
    d->format = audio_format(s_list[track].path);
    int r = d->format == AUDIO_MP3 ? open_mp3(d) : d->format == AUDIO_FLAC ? open_flac(d) : open_aac(d);
    if (r != 0) {
        dec_close(d);
        return -1;
    }
    if (d->hold > HOLD_MAX)
        d->hold = HOLD_MAX;
    d->skip = d->skip0;
    return 0;
}

/* Decodes one MP3 or AAC frame onto the end of the stage. 1 = went on (maybe
 * with nothing new), 0 = the end, -1 = an error. */
static int dec_unit(Dec *d)
{
    if (d->at > 0) { /* what is left to the front */
        memmove(d->stage, d->stage + d->at * 2, (size_t)(d->have - d->at) * 4);
        d->have -= d->at;
        d->at = 0;
    }
    int16_t *dst = d->stage + d->have * 2;
    if (d->aac) {
        Aac *a = d->aac;
        int size = aac_frame(d);
        if (size <= 0)
            return size;
        a->codec[6] = (unsigned long)a->in;
        a->codec[7] = (unsigned long)size;
        a->codec[8] = (unsigned long)a->pcm;
        a->codec[9] = AAC_SAMPLES * 4;
        int r = sceAudiocodecDecode(a->codec, PSP_CODEC_AAC);
        if (r < 0) {
            if (a->errors++ < 5) video_note("aac: Decode = %08X", r);
            memset(dst, 0, AAC_SAMPLES * 4);
        } else {
            sceKernelDcacheInvalidateRange(a->pcm, sizeof(a->pcm)); /* the decoder wrote past the cache */
            memcpy(dst, a->pcm, AAC_SAMPLES * 4);
        }
        d->have += AAC_SAMPLES;
        d->decoded += AAC_SAMPLES;
        return 1;
    }
    if (sceMp3CheckStreamDataNeeded(d->handle) > 0 && feed(d) < 0)
        return -1;
    SceShort16 *pcm;
    int bytes = sceMp3Decode(d->handle, &pcm);
    if (bytes <= 0 && d->seek_grace > 0) { /* just after a seek: frames missing the one before */
        d->seek_grace--;
        return 1;
    }
    if (bytes == 0 || (u32)bytes == MP3_END)
        return 0;
    if (bytes < 0)
        return -1;
    int out_ch = d->channels; /* the decoder may hand mono files back as stereo */
    if (d->spf && bytes == d->spf * 4) out_ch = 2;
    else if (d->spf && bytes == d->spf * 2) out_ch = 1;
    else if (bytes / (2 * out_ch) > MAX_SAMPLES) out_ch = 2;
    int samples = bytes / (2 * out_ch);
    if (samples <= 0 || samples > MAX_SAMPLES)
        return -1;
    if (d->xing_total && d->from_byte == d->audio_at) { /* from the start: never more than the header counts */
        if (d->since >= d->xing_total)
            return 0;
        if (d->since + (uint64_t)samples > d->xing_total)
            samples = (int)(d->xing_total - d->since);
    }
    if (out_ch == 1)
        for (int i = 0; i < samples; i++)
            dst[i * 2] = dst[i * 2 + 1] = pcm[i];
    else
        memcpy(dst, pcm, (size_t)samples * 4);
    d->have += samples;
    d->decoded += (uint64_t)samples;
    d->since += (uint64_t)samples;
    if (d->seek_grace > 0)
        d->seek_grace--;
    return 1;
}

/* Up to n frames of the song at its own rate; fewer only at its end (d->ended). */
static int dec_read(Dec *d, int16_t *dst, int n)
{
    int got = 0;
    if (d->format == AUDIO_FLAC) {
        while (got < n && !d->ended) {
            if (d->fat >= d->fhave) {
                u32 t0 = sceKernelGetSystemTimeLow(), r0 = d->rtime;
                if (flac_decode(&d->flac) <= 0) {
                    d->ended = 1; /* the end of the song */
                    break;
                }
                d->fhave = flac_output(&d->flac, d->fpcm);
                d->fat = 0;
                d->decoded += (uint64_t)d->fhave;
                u32 t = sceKernelGetSystemTimeLow() - t0 - (d->rtime - r0); /* the reading is counted on its own */
                if (t > d->fmax) d->fmax = t;
            }
            int k = d->fhave - d->fat;
            if (k > n - got) k = n - got;
            memcpy(dst + got * 2, d->fpcm + d->fat * 2, (size_t)k * 4);
            got += k;
            d->fat += k;
        }
        d->taken += (uint64_t)got;
        return got;
    }
    int idle = 0;
    while (got < n) {
        int avail = d->have - d->at;
        if (d->skip > 0 && avail > 0) { /* the encoder's silence at the start */
            int k = avail < d->skip ? avail : d->skip;
            d->at += k;
            d->skip -= k;
            continue;
        }
        int usable = avail - d->hold; /* the last `hold` frames are the encoder's padding */
        if (usable > 0) {
            int k = usable < n - got ? usable : n - got;
            memcpy(dst + got * 2, d->stage + d->at * 2, (size_t)k * 4);
            d->at += k;
            got += k;
            continue;
        }
        if (d->ended)
            break;
        int r = dec_unit(d);
        if (r < 0 || (r > 0 && ++idle > 64)) { /* a broken file: what was decoded still plays */
            d->failed = 1;
            r = 0;
        }
        if (r == 0)
            d->ended = 1; /* what is still in the stage is the padding at the end */
        if (r > 0 && d->have - d->at > 0) idle = 0;
    }
    d->taken += (uint64_t)got;
    return got;
}

/* Up to n frames at the output's rate (resampled if the song's isn't it). */
static int dec_pull(Dec *d, int16_t *dst, int n, int out_rate)
{
    if (d->rate == out_rate)
        return dec_read(d, dst, n);
    if (!d->rs_on) {
        rs_init(&d->rs, d->rate, out_rate);
        d->rs_on = 1;
        d->rs_have = d->rs_at = 0;
        video_note("song %d: %d Hz resampled to %d Hz", d->track, d->rate, out_rate);
    }
    int made = 0;
    while (made < n) {
        if (d->rs_at >= d->rs_have) {
            d->rs_have = dec_read(d, d->rsbuf, RS_IN);
            d->rs_at = 0;
            if (!d->rs_have)
                break;
        }
        int used;
        made += rs_run(&d->rs, d->rsbuf + d->rs_at * 2, d->rs_have - d->rs_at, dst + made * 2, n - made, &used);
        d->rs_at += used;
    }
    return made;
}

static int dec_elapsed(const Dec *d)
{
    return d->rate > 0 ? d->base_ms + (int)((long long)d->taken * 1000 / d->rate) : d->base_ms;
}

/* The song's volume-levelling gain (Q15), worked out again when Settings changes the mode. */
static int dec_level(Dec *d)
{
    if (d->level_mode != g_cfg.level) {
        d->level_mode = g_cfg.level;
        int g = audio_gain(&d->tags, d->level_mode);
        int v = audio_level_volume(g, PSP_AUDIO_VOLUME_MAX);
        d->level = (int)((long long)v * Q15 / PSP_AUDIO_VOLUME_MAX);
        if (g != GAIN_NONE)
            video_note("level (by %s): gain %s%d.%02d dB -> %d%%", d->level_mode == LEVEL_ALBUM ? "album" : "song",
                       g < 0 ? "-" : "+", (g < 0 ? -g : g) / 100, (g < 0 ? -g : g) % 100, d->level * 100 / Q15);
    }
    return d->level;
}

/* First frame at or after pos that matches the track's first frame (and is followed by one). */
static u32 find_frame(Dec *d, u32 pos)
{
    static unsigned char b[4096];
    sceIoLseek32(d->fd, pos, PSP_SEEK_SET);
    int n = sceIoRead(d->fd, b, sizeof(b));
    const u32 same = 0x001E0C00; /* version, layer, sample rate */
    for (int i = 0; i + 4 <= n; i++) {
        u32 h = be32(b + i);
        int len = frame_len(h);
        if (!len || (d->first_hdr && ((h ^ d->first_hdr) & same)))
            continue;
        if (i + len + 4 <= n) {
            u32 next = be32(b + i + len);
            if (!frame_len(next) || ((next ^ h) & same))
                continue;
        }
        return pos + i;
    }
    return pos;
}

static int alt_seek(Dec *d, int ms)
{
    int base = ms;
    if (d->format == AUDIO_FLAC) {
        if (flac_seek(&d->flac, (uint64_t)ms * (uint32_t)d->flac.rate / 1000) != 0)
            return -1;
        d->fhave = d->fat = 0;
    } else if (d->aac->has_m4a) {
        d->aac->sample = mp4_sample_at(&d->aac->m4a.audio, (uint32_t)ms);
        base = (int)mp4_time_ms(&d->aac->m4a.audio, d->aac->sample);
    } else {
        Aac *a = d->aac;
        uint64_t span = (uint64_t)d->size - a->adts_start;
        uint64_t pos = a->adts_start + span * (uint32_t)ms / (uint32_t)d->duration_ms;
        uint32_t left = (uint32_t)(d->size - pos), look = left < ADTS_LOOK ? left : ADTS_LOOK;
        if (look < 7 || readwin_get(&a->rw, pos, look, a->in) != 0)
            return -1;
        int at = adts_find(a->in, (int)look);
        if (at < 0)
            return -1;
        a->adts_pos = pos + (uint32_t)at;
    }
    d->base_ms = base;
    return 0;
}

/* 0 = done, -1 = past the end (go to the next song). */
static int dec_seek(Dec *d, int ms)
{
    if (d->duration_ms <= 0)
        return 0;
    if (ms >= d->duration_ms - 500)
        return -1;
    if (ms < 0)
        ms = 0;
    d->have = d->at = 0;
    d->ended = 0;
    d->taken = 0;
    d->rs_on = 0;
    d->skip = ms == 0 ? d->skip0 : 0; /* the padding only at the very start */
    if (d->format != AUDIO_MP3)
        return alt_seek(d, ms);
    u32 span = d->audio_end - d->start, pos;
    if (d->has_toc) { /* VBR: the Xing table maps each percent of time to a byte position */
        int pct10 = (int)((long long)ms * 1000 / d->duration_ms); /* tenths of a percent */
        int i = pct10 / 10 < 99 ? pct10 / 10 : 99, frac = pct10 % 10;
        int a = d->toc[i], b = i < 99 ? d->toc[i + 1] : 256;
        pos = d->start + (u32)(((long long)(a * 10 + (b - a) * frac) * span) / 2560);
    } else {
        pos = d->start + (u32)((long long)ms * span / d->duration_ms);
    }
    u32 frame = ms > 0 ? find_frame(d, pos) : d->audio_at;
    if (frame < d->audio_at)
        frame = d->audio_at;
    if (start_decoder(d, frame) < 0) {
        if (start_decoder(d, d->audio_at) < 0)
            return -1;
        frame = d->audio_at;
    }
    if (frame == d->audio_at) {
        d->base_ms = 0;
        d->skip = d->skip0;
    } else {
        d->base_ms = ms;
    }
    d->seek_grace = 8;
    return 0;
}

/* ------------------------------------------------------------- the output */

static int s_out_rate, s_chunk;        /* the output channel: its rate and frames per block */
static int16_t s_blk[OUT_BUFS][ALT_MAX * 2] __attribute__((aligned(64)));
static int s_blk_idx;
static int32_t s_acc[ALT_MAX * 2];
static int16_t s_tmp[ALT_MAX * 2] __attribute__((aligned(64)));
static int s_dry, s_fresh;             /* times the output ran out; 1 until the first block after a start or seek */

static void src_release(void)
{
    if (!s_out_rate)
        return;
    for (int i = 0; i < 100 && sceAudioSRCChRelease() < 0; i++) /* busy while the last buffer plays */
        sceKernelDelayThread(2000);
    s_out_rate = s_chunk = 0;
}

/* The output channel at `rate`, with the longest blocks the hardware takes. */
static int out_reserve(int rate)
{
    if (s_chunk && s_out_rate == rate)
        return 1;
    src_release();
    static const int sizes[] = {ALT_MAX, 2048, AAC_SAMPLES};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
        if (sceAudioSRCChReserve(sizes[i], rate, 2) >= 0) {
            s_out_rate = rate;
            s_chunk = sizes[i];
            return 1;
        }
    return 0;
}

/* ------------------------------------------------------------- the loader */

/* The next song is opened on its own thread, so the file, its tags and the
 * decoder's start never hold up the sound. The player thread owns the slot
 * again once the state isn't LOAD_BUSY. */
enum { LOAD_IDLE, LOAD_BUSY, LOAD_READY, LOAD_FAILED };
static volatile int s_load_state = LOAD_IDLE, s_load_slot = -1, s_load_track, s_load_pos, s_load_quit;
static SceUID s_load_sema = -1, s_load_thread = -1;

static int loader_thread(SceSize args, void *argp)
{
    for (;;) {
        sceKernelWaitSema(s_load_sema, 1, NULL);
        if (s_load_quit)
            return 0;
        if (s_load_state != LOAD_BUSY)
            continue;
        u32 t0 = sceKernelGetSystemTimeLow();
        int r = dec_open(&s_dec[s_load_slot], s_load_track, s_load_pos);
        video_note("next song %d opened ahead in %u ms%s", s_load_track, (sceKernelGetSystemTimeLow() - t0) / 1000,
                   r ? ": it won't play" : "");
        s_load_state = r == 0 ? LOAD_READY : LOAD_FAILED;
    }
}

static void load_start(int slot, int pos)
{
    s_load_slot = slot;
    s_load_pos = pos;
    s_load_track = s_order[pos];
    s_load_state = LOAD_BUSY;
    sceKernelSignalSema(s_load_sema, 1);
}

static void load_wait(void)
{
    for (int i = 0; i < 1500 && s_load_state == LOAD_BUSY; i++)
        sceKernelDelayThread(2000);
}

/* Forgets the song opened ahead. If the loader is still opening it, this waits for
 * it however long that takes: the two threads must never have one Dec at once. */
static void load_cancel(void)
{
    if (s_load_state == LOAD_IDLE)
        return;
    while (s_load_state == LOAD_BUSY)
        sceKernelDelayThread(2000);
    if (s_load_slot >= 0)
        dec_close(&s_dec[s_load_slot]);
    s_load_state = LOAD_IDLE;
    s_load_slot = -1;
}

/* ------------------------------------------------------------ the player */

static int s_cur = -1;                 /* the slot of the song shown as playing */
static int s_old = -1;                 /* the slot of the song fading out, -1 if none */
static int s_fade_pos, s_fade_len;     /* frames at the output's rate; pos >= len = no fade */
static int s_next_from = -1;           /* the play-order position the next song is looked for after */
static int s_load_fails;               /* songs in a row that wouldn't open ahead */
static int s_silent;                   /* songs in a row that ended without a sound */

static void show(Dec *d)
{
    s_current = d->track;
    s_pos = d->pos;
    s_duration_ms = d->duration_ms;
    s_elapsed_ms = dec_elapsed(d);
    s_next_from = d->pos;
    s_load_fails = 0;
    bump();
}

static void stop_playback(void)
{
    load_cancel();
    for (int i = 0; i < 2; i++)
        dec_close(&s_dec[i]);
    src_release();
    s_cur = s_old = -1;
    s_fade_pos = s_fade_len = 0;
    s_state = MUSIC_STOPPED;
    s_current = -1;
    s_pos = -1;
    s_elapsed_ms = s_duration_ms = 0;
    bump();
}

/* After a break in the sound (a start, skip or seek): the channel at the song's
 * own rate, so nothing is resampled, and the equalizer starts clean. */
static int realign(Dec *d)
{
    d->rs_on = 0;
    s_fresh = 1;
    eq_reset(&s_eq);
    if (s_dry)
        video_note("output ran dry %d times", s_dry);
    s_dry = 0;
    return out_reserve(d->rate);
}

/* Starts the song at position pos of the play order, skipping files that won't
 * play (at most a full round). A song already opened ahead for that place is used. */
static void start_pos(int pos)
{
    if (pos < 0 || pos >= s_count || !s_order) {
        stop_playback();
        return;
    }
    if (s_old >= 0) {
        dec_close(&s_dec[s_old]);
        s_old = -1;
    }
    s_fade_pos = s_fade_len = 0;
    if (s_load_state != LOAD_IDLE) {
        load_wait();
        if (s_load_state == LOAD_READY && s_load_pos == pos && s_load_track == s_order[pos]) {
            if (s_cur >= 0) dec_close(&s_dec[s_cur]);
            s_cur = s_load_slot;
            s_load_state = LOAD_IDLE;
            s_load_slot = -1;
            if (realign(&s_dec[s_cur])) {
                s_state = MUSIC_PLAYING;
                show(&s_dec[s_cur]);
                return;
            }
        }
        load_cancel();
    }
    if (s_cur < 0) s_cur = 0;
    for (int tries = 0; tries < s_count; tries++, pos = (pos + 1) % s_count) {
        if (dec_open(&s_dec[s_cur], s_order[pos], pos) == 0 && realign(&s_dec[s_cur])) {
            s_state = MUSIC_PLAYING;
            s_silent = 0;
            show(&s_dec[s_cur]);
            return;
        }
    }
    stop_playback();
}

/* ended: the song finished by itself (repeat one plays it again). */
static void advance(int ended)
{
    int next = order_next(s_pos, s_count, s_repeat, ended);
    if (next < 0) {
        stop_playback(); /* the end of the list */
        s_ended = 1;
    } else {
        start_pos(next);
    }
}

/* Where the song after the playing one is: -1 at the end of the list. */
static int next_wanted(void)
{
    return s_next_from >= 0 ? order_next(s_next_from, s_count, s_repeat, 1) : -1;
}

/* Keeps the song opened ahead right: opens it when the playing one nears its end,
 * forgets it if shuffle or repeat changed which song comes next, and passes over
 * one that won't open. */
static void preload(void)
{
    Dec *c = &s_dec[s_cur];
    int want = next_wanted();
    if (s_load_state == LOAD_READY || s_load_state == LOAD_BUSY) {
        if (s_load_pos != want || (want >= 0 && s_load_track != s_order[want]))
            load_cancel(); /* not the next one any more */
        return;
    }
    if (s_load_state == LOAD_FAILED) {
        s_load_state = LOAD_IDLE;
        s_load_slot = -1;
        if (++s_load_fails >= s_count) {
            s_next_from = -1; /* nothing in the list opens: it ends after this song */
            return;
        }
        s_next_from = s_load_pos; /* the one after it */
        want = next_wanted();
    }
    if (want < 0 || s_old >= 0) /* the end of the list, or both slots busy with a crossfade */
        return;
    int left = c->duration_ms - dec_elapsed(c);
    if (c->duration_ms > 0 && !c->ended && left > g_cfg.crossfade * 1000 + PRELOAD_MS)
        return;
    load_start(1 - s_cur, want);
}

/* How long the crossfade into the song opened ahead is to be (0 = gapless). */
static int fade_ms_for(Dec *c, Dec *n)
{
    const Track *a = &s_list[c->track], *b = &s_list[n->track];
    GapSong sa = {a->path, a->album, a->tracknum, c->track}, sb = {b->path, b->album, b->tracknum, n->track};
    int same = gap_same_album(&sa, &sb), rep1 = s_repeat == REPEAT_ONE && c->track == n->track;
    return gap_fade_ms(g_cfg.crossfade, rep1, same, c->duration_ms > 0 ? c->duration_ms : 0, n->duration_ms);
}

/* The song opened ahead becomes the playing one. */
static void take_next(void)
{
    s_cur = s_load_slot;
    s_load_state = LOAD_IDLE;
    s_load_slot = -1;
    show(&s_dec[s_cur]);
}

/* Mixes up to n frames of d into the block at `at`; pos is where that is in the
 * fade (in or out). Returns the frames d gave. */
static int add(Dec *d, int at, int n, int pos, int fade_in)
{
    int lvl = dec_level(d);
    int got = dec_pull(d, s_tmp, n, s_out_rate);
    if (got <= 0)
        return 0;
    int g0 = lvl, g1 = lvl;
    if (pos < s_fade_len) {
        int a = fade_in ? xf_gain(pos, s_fade_len) : xf_gain(s_fade_len - pos, s_fade_len);
        int b = fade_in ? xf_gain(pos + got, s_fade_len) : xf_gain(s_fade_len - pos - got, s_fade_len);
        g0 = (int)((long long)a * lvl >> 15);
        g1 = (int)((long long)b * lvl >> 15);
    }
    mix_add(s_acc + at * 2, s_tmp, got, g0, g1);
    return got;
}

/* Makes and plays one block. 1 = went on, 0 = the music ended. */
static int play_block(void)
{
    int n = s_chunk;
    if (!n)
        return 0;
    memset(s_acc, 0, sizeof(s_acc[0]) * (size_t)n * 2);
    Dec *c = &s_dec[s_cur];
    /* time for the crossfade into the next song? */
    if (s_old < 0 && s_load_state == LOAD_READY && c->duration_ms > 0) {
        Dec *nx = &s_dec[s_load_slot];
        int ms = fade_ms_for(c, nx), left = c->duration_ms - dec_elapsed(c);
        if (ms > 0 && left <= ms) {
            if (left < 200) left = 200;
            s_old = s_cur;
            s_fade_len = (int)((long long)left * s_out_rate / 1000);
            s_fade_pos = 0;
            video_note("crossfade %d ms into song %d", left, nx->track);
            take_next();
            c = &s_dec[s_cur];
        }
    }
    int fade_at = s_fade_pos, filled = 0;
    /* the playing song, and right after its end the next one, without a gap */
    for (int guard = 0; filled < n && guard < 4; guard++) {
        uint64_t t0 = c->taken;
        filled += add(c, filled, n - filled, fade_at + filled, 1);
        if (c->taken > t0)
            s_silent = 0;
        if (filled >= n || !c->ended)
            break;
        /* the playing song is over */
        if (c->taken == 0 && ++s_silent >= 8) { /* broken files in a row: give up */
            video_note("%d songs in a row had no sound: stopping", s_silent);
            s_next_from = -1;
        }
        if (s_old >= 0) /* it ended during its own fade-in (a very short song): the fade carries on */
            break;
        if (s_load_state == LOAD_IDLE || s_load_state == LOAD_FAILED)
            preload();
        if (s_load_state == LOAD_BUSY)
            load_wait(); /* it should have been ready long ago (an unknown length, or a slow stick) */
        if (s_load_state == LOAD_BUSY) {
            filled = n; /* still not open: a moment of silence, and the next block tries again */
            break;
        }
        if (s_load_state != LOAD_READY)
            break; /* nothing comes next */
        dec_close(c);
        take_next(); /* at another rate it is resampled until the next break in the sound */
        c = &s_dec[s_cur];
    }
    /* the old song fading out */
    if (s_old >= 0) {
        Dec *o = &s_dec[s_old];
        add(o, 0, n, fade_at, 0);
        if (o->ended || fade_at + n >= s_fade_len) {
            dec_close(o);
            s_old = -1;
        }
        filled = n;
    }
    if (s_fade_pos < s_fade_len)
        s_fade_pos += n;
    if (!filled)
        return 0;
    int16_t *out = s_blk[s_blk_idx];
    s_blk_idx = (s_blk_idx + 1) % OUT_BUFS;
    mix_out(out, s_acc, n);
    int g[EQ_BANDS];
    if (eq_wanted(g))
        eq_process(&s_eq, out, n, s_out_rate, g);
    sceKernelDcacheWritebackRange(out, (size_t)n * 4);
    u32 t0 = sceKernelGetSystemTimeLow();
    sceAudioSRCOutputBlocking(s_volume, out);
    if (!s_fresh && sceKernelGetSystemTimeLow() - t0 < 500) /* it didn't have to wait: the sound ran dry */
        s_dry++;
    s_fresh = 0;
    s_elapsed_ms = dec_elapsed(&s_dec[s_cur]);
    return 1;
}

/* Shuffle switched while a list is loaded: mix (the current song first) or put it back in order. */
static void reorder(int on)
{
    if (!s_order || s_count <= 0)
        return;
    int cur = s_current;
    play_order(s_order, s_count, on, cur >= 0 ? cur : 0, &s_seed);
    if (cur >= 0) {
        s_pos = on ? 0 : cur;
        if (s_cur >= 0) s_dec[s_cur].pos = s_pos;
        s_next_from = s_pos;
    }
    bump(); /* the song opened ahead is checked against the new order at the next block */
}

static void handle(int cmd, int arg)
{
    switch (cmd) {
    case CMD_PLAY: start_pos(arg); break;
    case CMD_TOGGLE:
        s_fresh = 1; /* a pause empties the output: not a dry spell */
        if (s_state == MUSIC_PLAYING) s_state = MUSIC_PAUSED;
        else if (s_state == MUSIC_PAUSED) s_state = MUSIC_PLAYING;
        bump();
        break;
    case CMD_NEXT:
        if (s_current >= 0) advance(0);
        break;
    case CMD_PREV:
        if (s_current < 0) break;
        if (s_elapsed_ms > 3000) start_pos(s_pos); /* back to the start of this one */
        else start_pos(order_prev(s_pos, s_count, s_repeat));
        break;
    case CMD_SEEK: {
        if (s_current < 0 || s_cur < 0) break;
        if (s_old >= 0) { /* a seek ends a crossfade */
            dec_close(&s_dec[s_old]);
            s_old = -1;
        }
        s_fade_pos = s_fade_len = 0;
        Dec *c = &s_dec[s_cur];
        if (dec_seek(c, dec_elapsed(c) + arg) < 0) {
            advance(0);
        } else {
            realign(c);
            s_elapsed_ms = dec_elapsed(c);
            bump();
        }
        break;
    }
    case CMD_STOP: stop_playback(); break;
    case CMD_SHUFFLE: reorder(arg); break;
    }
}

static int player_thread(SceSize args, void *argp)
{
    for (;;) {
        while (s_q_tail != s_q_head) {
            int t = s_q_tail, cmd = s_q_cmd[t];
            if (cmd == CMD_QUIT) {
                stop_playback();
                s_done_seq = s_q_seq[t];
                return 0;
            }
            handle(cmd, s_q_arg[t]);
            s_done_seq = s_q_seq[t];
            s_q_tail = (t + 1) % QUEUE;
        }
        if (s_state != MUSIC_PLAYING || s_cur < 0 || s_dec[s_cur].track < 0) {
            sceKernelDelayThread(10000);
            continue;
        }
        preload();
        if (!play_block()) { /* the playing song ended and nothing follows */
            if (next_wanted() < 0 && s_next_from >= 0) {
                stop_playback(); /* the end of the list */
                s_ended = 1;
            } else {
                stop_playback();
            }
        }
    }
}

/* ---------------------------------------------------------------- the UI */

static int post(int cmd, int arg)
{
    if (s_thread < 0)
        return -1;
    int h = s_q_head, n = (h + 1) % QUEUE;
    if (n == s_q_tail)
        return -1; /* full: dropped */
    s_q_cmd[h] = cmd;
    s_q_arg[h] = arg;
    s_q_seq[h] = ++s_seq;
    s_q_head = n;
    return s_seq;
}

static void wait_done(int seq)
{
    for (int i = 0; seq > 0 && s_done_seq < seq && i < 500; i++)
        sceKernelDelayThread(1000);
}

static void stop_loader(void)
{
    if (s_load_thread < 0)
        return;
    s_load_quit = 1;
    sceKernelSignalSema(s_load_sema, 1);
    SceUInt timeout = 2000000;
    sceKernelWaitThreadEnd(s_load_thread, &timeout);
    sceKernelDeleteThread(s_load_thread);
    sceKernelDeleteSema(s_load_sema);
    s_load_thread = s_load_sema = -1;
}

static int load_modules(void)
{
    if (s_ready)
        return 0;
    if (sceKernelGetCompiledSdkVersion() == 0)
        sceKernelSetCompiledSdkVersion(0x06060010); /* or sceMp3 only takes 44.1 kHz */
    int r = sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
    s_loaded_avcodec = r >= 0;
    if (r < 0 && (u32)r != ALREADY_LOADED && (u32)r != ALREADY_LOADED2)
        return -1;
    r = sceUtilityLoadModule(PSP_MODULE_AV_MP3);
    s_loaded_mp3 = r >= 0;
    if (r < 0 && (u32)r != ALREADY_LOADED && (u32)r != ALREADY_LOADED2)
        goto fail;
    if (sceMp3InitResource() < 0)
        goto fail;
    s_q_head = s_q_tail = 0;
    s_done_seq = s_seq;
    s_load_quit = 0;
    s_load_state = LOAD_IDLE;
    s_load_sema = sceKernelCreateSema("xl_load", 0, 0, 8, NULL);
    /* below the player (it must never wait for a song being opened) but above the UI */
    s_load_thread = s_load_sema >= 0 ? sceKernelCreateThread("xl_musload", loader_thread, 0x18, 0x4000, PSP_THREAD_ATTR_USER, NULL) : -1;
    if (s_load_thread < 0 || sceKernelStartThread(s_load_thread, 0, NULL) < 0) {
        if (s_load_thread >= 0) sceKernelDeleteThread(s_load_thread);
        if (s_load_sema >= 0) sceKernelDeleteSema(s_load_sema);
        s_load_thread = s_load_sema = -1;
        sceMp3TermResource();
        goto fail;
    }
    s_thread = sceKernelCreateThread("xl_music", player_thread, 0x14, 0x4000, PSP_THREAD_ATTR_USER, NULL);
    if (s_thread < 0 || sceKernelStartThread(s_thread, 0, NULL) < 0) {
        if (s_thread >= 0) sceKernelDeleteThread(s_thread);
        s_thread = -1;
        stop_loader();
        sceMp3TermResource();
        goto fail;
    }
    s_ready = 1;
    return 0;
fail:
    if (s_loaded_mp3) sceUtilityUnloadModule(PSP_MODULE_AV_MP3);
    if (s_loaded_avcodec) sceUtilityUnloadModule(PSP_MODULE_AV_AVCODEC);
    s_loaded_mp3 = s_loaded_avcodec = 0;
    return -1;
}

/* ------------------------------------------------------- the saved place */

static u32 now_ms(void) { return sceKernelGetSystemTimeLow() / 1000; }

static int read_text(const char *path, char **out)
{
    *out = NULL;
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return -1;
    int size = sceIoLseek32(fd, 0, PSP_SEEK_END);
    sceIoLseek32(fd, 0, PSP_SEEK_SET);
    char *b = size >= 0 && size < 1024 * 1024 ? malloc(size + 1) : NULL;
    int n = b ? sceIoRead(fd, b, size) : -1;
    sceIoClose(fd);
    if (n < 0) {
        free(b);
        return -1;
    }
    b[n] = 0;
    *out = b;
    return n;
}

static void write_text(const char *path, const char *text, int len)
{
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0)
        return;
    sceIoWrite(fd, text, len);
    sceIoClose(fd);
}

typedef struct {
    char path[220];
    char text[512];
    int len;
} StateArg;

static void job_write_state(void *arg)
{
    const StateArg *a = arg;
    write_text(a->path, a->text, a->len);
}

/* Writes s_saved: on the worker, or right away (sync) before the stick goes away. */
static void save_state(int sync)
{
    if (!s_cache[0])
        return;
    static StateArg a;
    snprintf(a.path, sizeof(a.path), "%s/music.txt", s_cache);
    a.len = resume_write_state(a.text, sizeof(a.text), &s_saved);
    if (a.len < 0)
        return;
    if (sync) write_text(a.path, a.text, a.len);
    else bg_post(job_write_state, &a, sizeof(a));
}

typedef struct {
    char *text;
    int len;
    char path[220];
} ListArg;

static void job_write_list(void *arg)
{
    const ListArg *a = arg;
    write_text(a->path, a->text, a->len);
    free(a->text);
}

/* The new list, for music_resume (on the worker). */
static void save_list(void)
{
    if (!s_cache[0])
        return;
    int cap = s_count * (int)(sizeof(Track) + 4) + 1;
    ListArg a;
    a.text = malloc(cap);
    if (!a.text)
        return;
    a.len = 0;
    for (int i = 0; i < s_count; i++) {
        int n = resume_write_song(a.text + a.len, cap - a.len, s_list[i].path, s_list[i].title, s_list[i].artist);
        if (n > 0) a.len += n;
    }
    snprintf(a.path, sizeof(a.path), "%s/music_list.txt", s_cache);
    bg_post(job_write_list, &a, sizeof(a)); /* the worker writes, then frees the text */
}

/* s_saved from what is playing now. */
static void note_place(void)
{
    int pos = s_pos;
    if (!s_list || !s_order || pos < 0 || pos >= s_count)
        return;
    int t = s_order[pos];
    s_saved.track = t;
    s_saved.ms = s_elapsed_ms;
    snprintf(s_saved.title, sizeof(s_saved.title), "%s", s_list[t].title);
    snprintf(s_saved.artist, sizeof(s_saved.artist), "%s", s_list[t].artist);
}

void music_init(const char *app_dir)
{
    snprintf(s_cache, sizeof(s_cache), "%.180scache", app_dir);
    s_seed = sceKernelGetSystemTimeLow();
    char path[220], *text;
    snprintf(path, sizeof(path), "%s/music.txt", s_cache);
    if (read_text(path, &text) >= 0) {
        resume_parse_state(text, &s_saved);
        free(text);
    } else {
        resume_parse_state("", &s_saved);
    }
    s_shuffle = s_saved.shuffle;
    s_repeat = s_saved.repeat;
}

/* The sleep timer: the last 10 s fade out, then the music stops (its place kept). */
static void sleep_tick(void)
{
    if (!s_sleep_at)
        return;
    int left = (int)(s_sleep_at - now_ms());
    if (s_state == MUSIC_STOPPED || left <= 0) {
        if (s_state != MUSIC_STOPPED) {
            note_place();
            save_state(0);
            post(CMD_STOP, 0);
            ui_toast("Sleep timer: music stopped");
        }
        s_sleep_at = 0;
        s_sleep_min = 0;
        s_volume = PSP_AUDIO_VOLUME_MAX;
        bump();
        return;
    }
    s_volume = left < 10000 ? PSP_AUDIO_VOLUME_MAX * left / 10000 : PSP_AUDIO_VOLUME_MAX;
}

static const int SLEEP_STEPS[] = {0, 15, 30, 45, 60, 90};

int music_sleep_cycle(void)
{
    int i = 0;
    while (i < 5 && SLEEP_STEPS[i] != s_sleep_min) i++;
    s_sleep_min = SLEEP_STEPS[(i + 1) % 6];
    s_sleep_at = s_sleep_min ? now_ms() + (u32)s_sleep_min * 60000u : 0;
    if (!s_sleep_at) s_volume = PSP_AUDIO_VOLUME_MAX;
    bump();
    return s_sleep_min;
}

int music_sleep_left(void)
{
    if (!s_sleep_at)
        return 0;
    int left = (int)(s_sleep_at - now_ms());
    return left > 0 ? (left + 59999) / 60000 : 0;
}

void music_poll(void)
{
    static int last_pos = -2, last_state = -1;
    static u32 last_save;
    if (s_open_err) { /* a song that couldn't be played was passed over */
        char msg[80];
        snprintf(msg, sizeof(msg), "Skipped a song: %s", s_open_err);
        s_open_err = NULL;
        ui_toast(msg);
    }
    if (!s_cache[0])
        return;
    sleep_tick();
    if (s_ended) { /* played to the end: next time starts fresh */
        s_ended = 0;
        s_saved.track = -1;
        s_saved.ms = 0;
        save_state(0);
        last_pos = -1;
        last_state = s_state;
        return;
    }
    int st = s_state, pos = s_pos;
    if (pos < 0)
        return;
    u32 t = now_ms();
    if (pos != last_pos || st != last_state || (st == MUSIC_PLAYING && t - last_save > 20000)) {
        last_pos = pos;
        last_state = st;
        last_save = t;
        note_place();
        save_state(0);
    }
}

/* ------------------------------------------------------------ new lists */

/* Stops the player and makes room for a list of n songs (at most MAX_TRACKS).
 * Returns the count kept, or 0. */
static int begin_list(int n)
{
    if (video_is_open()) /* the player has the ME and the audio: songs would only fail one after another */
        return 0;
    video_preview_stop(1); /* an MP4 preview has the ME booted its own way */
    if (n <= 0 || load_modules() != 0)
        return 0;
    wait_done(post(CMD_STOP, 0)); /* the player lets go of the old list */
    free(s_list);
    free(s_order);
    s_count = n < MAX_TRACKS ? n : MAX_TRACKS;
    s_list = malloc(sizeof(Track) * s_count);
    s_order = malloc(sizeof(int) * s_count);
    if (!s_list || !s_order) {
        free(s_list);
        free(s_order);
        s_list = NULL;
        s_order = NULL;
        s_count = 0;
    }
    return s_count;
}

/* Plays the filled list from track `index` (in the shuffle order if it's on). */
static void go(int index)
{
    play_order(s_order, s_count, s_shuffle, index, &s_seed);
    power_hold(222); /* steady clock while music plays; released when it stops */
    post(CMD_PLAY, s_shuffle ? 0 : index);
}

/* Replaces the playlist (after the player let go of the old one) and plays it from `index`. */
typedef void (*FillFn)(Track *t, int i, const void *ctx);

static void play_list(int n, int index, FillFn fill, const void *ctx)
{
    if (index < 0 || index >= n || !begin_list(n))
        return;
    /* More tracks than the cap: copy a window of MAX_TRACKS around the chosen one. */
    int first = 0;
    if (n > s_count) {
        first = index - s_count / 2;
        if (first < 0) first = 0;
        if (first + s_count > n) first = n - s_count;
    }
    for (int i = 0; i < s_count; i++)
        fill(&s_list[i], first + i, ctx);
    go(index - first);
    save_list();
}

static void fill_entry(Track *t, const Entry *e)
{
    snprintf(t->path, sizeof(t->path), "%s", e->path);
    snprintf(t->title, sizeof(t->title), "%s", e->title);
    snprintf(t->artist, sizeof(t->artist), "%s", e->artist);
    snprintf(t->album, sizeof(t->album), "%s", e->album);
    t->tracknum = e->track;
}

static void fill_view(Track *t, int i, const void *ctx)
{
    fill_entry(t, library_get(VIEW_MUSIC, i));
}

void music_play_view(int index)
{
    play_list(library_count(VIEW_MUSIC), index, fill_view, NULL);
}

static void fill_files(Track *t, int i, const void *ctx)
{
    const char *const *paths = ctx;
    const char *p = paths[i], *name = strrchr(p, '/');
    name = name ? name + 1 : p;
    const char *dot = strrchr(name, '.');
    snprintf(t->path, sizeof(t->path), "%s", p);
    snprintf(t->title, sizeof(t->title), "%.*s", dot ? (int)(dot - name) : (int)strlen(name), name);
    t->artist[0] = t->album[0] = 0;
    t->tracknum = 0;
    /* the library knows the tags if the file is in MUSIC/ */
    int k = library_find(VIEW_MUSIC, p);
    if (k >= 0)
        fill_entry(t, library_get(VIEW_MUSIC, k));
}

void music_play_files(const char *const *paths, int n, int index)
{
    play_list(n, index, fill_files, paths);
}

static void fill_entries(Track *t, int i, const void *ctx)
{
    fill_entry(t, (const Entry *)ctx + i);
}

void music_play_entries(const Entry *list, int n, int index)
{
    play_list(n, index, fill_entries, list);
}

typedef struct { Track *t; int n, cap; } Loaded;

static void add_loaded(const char *path, const char *title, const char *artist, void *ctx)
{
    Loaded *l = ctx;
    if (l->n >= l->cap)
        return;
    Track *t = &l->t[l->n++];
    snprintf(t->path, sizeof(t->path), "%s", path);
    snprintf(t->title, sizeof(t->title), "%s", title);
    snprintf(t->artist, sizeof(t->artist), "%s", artist);
    t->album[0] = 0;
    t->tracknum = 0;
    int k = library_find(VIEW_MUSIC, path); /* the album rule needs the tags, which the list file doesn't keep */
    if (k >= 0) {
        const Entry *e = library_get(VIEW_MUSIC, k);
        snprintf(t->album, sizeof(t->album), "%s", e->album);
        t->tracknum = e->track;
    }
}

int music_can_resume(const char **title, const char **artist)
{
    if (!s_cache[0] || s_state != MUSIC_STOPPED || s_saved.track < 0)
        return 0;
    if (title) *title = s_saved.title;
    if (artist) *artist = s_saved.artist;
    return 1;
}

int music_resume(void)
{
    if (!music_can_resume(NULL, NULL))
        return -1;
    char path[220], *text;
    snprintf(path, sizeof(path), "%s/music_list.txt", s_cache);
    bg_flush(); /* a list still being written goes first */
    int len = read_text(path, &text);
    int n = len > 0 ? resume_parse_list(text, len, NULL, NULL) : 0;
    if (n <= s_saved.track || !begin_list(n)) {
        free(text);
        if (n <= s_saved.track) {
            s_saved.track = -1; /* the list is gone or shorter: nothing to go back to */
            save_state(0);
        }
        return -1;
    }
    Loaded l = {s_list, 0, s_count};
    resume_parse_list(text, len, add_loaded, &l);
    free(text);
    s_count = l.n;
    int ms = s_saved.ms - 2000; /* a moment before, to pick up the thread */
    go(s_saved.track);
    if (ms > 3000)
        post(CMD_SEEK, ms);
    return 0;
}

void music_set_shuffle(int on)
{
    s_shuffle = on != 0;
    if (s_list && s_thread >= 0)
        post(CMD_SHUFFLE, s_shuffle);
    s_saved.shuffle = s_shuffle;
    save_state(0);
    bump();
}

int music_shuffle(void) { return s_shuffle; }

void music_set_repeat(int mode)
{
    s_repeat = mode >= 0 && mode < REPEAT_COUNT ? mode : REPEAT_OFF;
    s_saved.repeat = s_repeat;
    save_state(0);
    bump();
}

int music_repeat(void) { return s_repeat; }

int music_peek(int ahead, const char **title, const char **artist)
{
    int pos = s_pos;
    if (pos < 0 || !s_list || !s_order)
        return -1;
    for (int i = 0; i < ahead && pos >= 0; i++)
        pos = order_next(pos, s_count, s_repeat, 1);
    if (pos < 0 || pos >= s_count)
        return -1;
    int t = s_order[pos];
    if (title) *title = s_list[t].title;
    if (artist) *artist = s_list[t].artist;
    return 0;
}

void music_toggle(void) { post(CMD_TOGGLE, 0); }
void music_next(void) { post(CMD_NEXT, 0); }
void music_prev(void) { post(CMD_PREV, 0); }
void music_seek(int delta_ms) { post(CMD_SEEK, delta_ms); }
void music_stop(void) { post(CMD_STOP, 0); }

void music_shutdown(void)
{
    if (!s_ready)
        return;
    if (s_pos >= 0) { /* where it was, written now: the stick is about to go to a game or the PC */
        note_place();
        save_state(1);
    }
    video_preview_stop(1); /* unloading AVCODEC would pull it from under a preview */
    int seq = post(CMD_QUIT, 0);
    wait_done(seq);
    SceUInt timeout = 1000000;
    sceKernelWaitThreadEnd(s_thread, &timeout);
    sceKernelDeleteThread(s_thread);
    s_thread = -1;
    stop_loader();
    sceMp3TermResource();
    if (s_loaded_mp3) sceUtilityUnloadModule(PSP_MODULE_AV_MP3);
    if (s_loaded_avcodec) sceUtilityUnloadModule(PSP_MODULE_AV_AVCODEC);
    s_loaded_mp3 = s_loaded_avcodec = 0;
    s_ready = 0;
    free(s_list);
    free(s_order);
    s_list = NULL;
    s_order = NULL;
    s_count = 0;
    s_state = MUSIC_STOPPED;
    s_current = -1;
    bump();
    power_hold(0);
}

int music_state(void) { return s_state; }
int music_generation(void) { return s_gen; }

int music_now(MusicNow *o)
{
    int i = s_current, pos = s_pos;
    if (i < 0 || i >= s_count || !s_list)
        return -1;
    o->title = s_list[i].title;
    o->artist = s_list[i].artist;
    o->path = s_list[i].path;
    o->index = pos >= 0 ? pos : i;
    o->count = s_count;
    o->elapsed_ms = s_elapsed_ms;
    o->duration_ms = s_duration_ms;
    return 0;
}
