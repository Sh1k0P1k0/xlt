/* xltcast - the game's sound (roadmap step C3). See audio.h.
 *
 * Games play sound through sceAudio's system calls: a channel (0-7) is reserved with
 * its block length and mono/stereo, then each call plays one block; the SRC channel
 * takes another sample rate, Output2 a fixed 44.1 kHz stereo one. Those calls are
 * patched in the syscall table (sctrlHENPatchSyscall) from the plugin's start, so the
 * channels' set-up is known when casting begins later. While casting, each block is
 * also added into a mix: a ring of 32-bit stereo samples on a timeline of the PSP's
 * clock (44100 a second), every channel writing at its own position just ahead of
 * "now". The sender takes what is behind "now" (everything for it has arrived), as
 * 16-bit stereo. Not casting, the play hooks only call the original.
 *
 * The patches stay until the PSP restarts for the next program (the plugin is never
 * stopped: its stop routine isn't called when a game quits). */
#include <pspkernel.h>
#include <pspsdk.h>
#include <pspsysmem_kernel.h>
#include <string.h>
#include <systemctrl.h>
#include "audio.h"

int snprintf(char *buf, unsigned n, const char *fmt, ...);
void log_line(const char *line, int truncate); /* main.c */

#define RATE 44100
#define MIXN 8192          /* ring: 186 ms; a power of two */
#define LEAD 1024          /* a channel (re)starts this far ahead of now: 23 ms */
#define NCH 10             /* 0-7 the channels, 8 SRC, 9 Output2 */
#define CH_SRC 8
#define CH_OUT2 9

static struct { int samples, stereo, rate; u64 pos; } s_ch[NCH];
static int *s_mix;         /* MIXN stereo frames, NULL while not casting */
static short *s_out;       /* AUDIO_TAKE_MAX stereo frames for the sender */
static SceUID s_block = -1;
static u64 s_rpos;         /* the next frame the sender takes */
static volatile unsigned s_taken, s_blocks;

static u64 now_frames(void) { return sceKernelGetSystemTimeWide() * RATE / 1000000; }

/* one block of a channel into the mix */
static void mix_in(int c, const short *buf, int lv, int rv)
{
    if (!s_mix || !buf || c < 0 || c >= NCH || s_ch[c].samples <= 0)
        return;
    int n = s_ch[c].samples, stereo = s_ch[c].stereo, rate = s_ch[c].rate ? s_ch[c].rate : RATE;
    if (lv > 0x8000) lv = 0x8000;
    if (rv > 0x8000) rv = 0x8000;
    if (lv < 0) lv = 0;
    if (rv < 0) rv = 0;
    int m = rate == RATE ? n : (int)((u64)n * RATE / rate);
    u64 now = now_frames();
    unsigned st = pspSdkDisableInterrupts();
    u64 pos = s_ch[c].pos;
    if (pos + MIXN / 4 < now || pos > now + MIXN / 2) /* new, or lost its place: just ahead of now */
        pos = now + LEAD;
    if (pos < s_rpos) /* never into what the sender took already */
        pos = s_rpos;
    for (int i = 0; i < m; i++) {
        int src = rate == RATE ? i : (int)((u64)i * rate / RATE);
        int l, r;
        if (stereo) { l = buf[2 * src]; r = buf[2 * src + 1]; }
        else l = r = buf[src];
        unsigned k = (unsigned)(pos + i) & (MIXN - 1);
        s_mix[2 * k] += l * lv >> 15;
        s_mix[2 * k + 1] += r * rv >> 15;
    }
    s_ch[c].pos = pos + m;
    s_blocks++;
    pspSdkEnableInterrupts(st);
}

/* ------------------------------------------------------------------ hooks */

#define NID_RESERVE      0x5EC81C55
#define NID_RELEASE      0x6FC46853
#define NID_DATALEN      0xCB2E439E
#define NID_CONFIG       0x95FD0C2D
#define NID_OUT          0x8C1009B2
#define NID_OUT_B        0x136CAF51
#define NID_PAN          0xE2D56B2D
#define NID_PAN_B        0x13F592BC
#define NID_SRC_RESERVE  0x38553111
#define NID_SRC_RELEASE  0x5C37C0AE
#define NID_SRC_OUT_B    0xE0727056
#define NID_O2_RESERVE   0x01562BA3
#define NID_O2_RELEASE   0x43196845
#define NID_O2_LENGTH    0x63F2889C
#define NID_O2_OUT_B     0x2D53F36E

static int (*o_reserve)(int, int, int), (*o_release)(int), (*o_datalen)(int, int), (*o_config)(int, int);
static int (*o_out)(int, int, void *), (*o_out_b)(int, int, void *);
static int (*o_pan)(int, int, int, void *), (*o_pan_b)(int, int, int, void *);
static int (*o_src_reserve)(int, int, int), (*o_src_release)(void), (*o_src_out_b)(int, void *);
static int (*o_o2_reserve)(int), (*o_o2_release)(void), (*o_o2_length)(int), (*o_o2_out_b)(int, void *);

/* PSP_AUDIO_FORMAT_STEREO 0, MONO 0x10 */
static int h_reserve(int ch, int samples, int fmt)
{
    int r = o_reserve(ch, samples, fmt);
    if (r >= 0 && r < 8) {
        s_ch[r].samples = samples;
        s_ch[r].stereo = fmt != 0x10;
        s_ch[r].rate = RATE;
        s_ch[r].pos = 0;
    }
    return r;
}
static int h_release(int ch)
{
    int r = o_release(ch);
    if (r >= 0 && ch >= 0 && ch < 8) s_ch[ch].samples = 0;
    return r;
}
static int h_datalen(int ch, int n)
{
    int r = o_datalen(ch, n);
    if (r >= 0 && ch >= 0 && ch < 8) s_ch[ch].samples = n;
    return r;
}
static int h_config(int ch, int fmt)
{
    int r = o_config(ch, fmt);
    if (r >= 0 && ch >= 0 && ch < 8) s_ch[ch].stereo = fmt != 0x10;
    return r;
}
static int h_out(int ch, int vol, void *buf) { mix_in(ch, buf, vol, vol); return o_out(ch, vol, buf); }
static int h_out_b(int ch, int vol, void *buf) { mix_in(ch, buf, vol, vol); return o_out_b(ch, vol, buf); }
static int h_pan(int ch, int l, int r, void *buf) { mix_in(ch, buf, l, r); return o_pan(ch, l, r, buf); }
static int h_pan_b(int ch, int l, int r, void *buf) { mix_in(ch, buf, l, r); return o_pan_b(ch, l, r, buf); }
static int h_src_reserve(int samples, int freq, int channels)
{
    int r = o_src_reserve(samples, freq, channels);
    if (r >= 0) {
        s_ch[CH_SRC].samples = samples;
        s_ch[CH_SRC].stereo = channels != 1;
        s_ch[CH_SRC].rate = freq > 0 ? freq : RATE;
        s_ch[CH_SRC].pos = 0;
    }
    return r;
}
static int h_src_release(void) { int r = o_src_release(); if (r >= 0) s_ch[CH_SRC].samples = 0; return r; }
static int h_src_out_b(int vol, void *buf) { mix_in(CH_SRC, buf, vol, vol); return o_src_out_b(vol, buf); }
static int h_o2_reserve(int samples)
{
    int r = o_o2_reserve(samples);
    if (r >= 0) {
        s_ch[CH_OUT2].samples = samples;
        s_ch[CH_OUT2].stereo = 1;
        s_ch[CH_OUT2].rate = RATE;
        s_ch[CH_OUT2].pos = 0;
    }
    return r;
}
static int h_o2_release(void) { int r = o_o2_release(); if (r >= 0) s_ch[CH_OUT2].samples = 0; return r; }
static int h_o2_length(int n) { int r = o_o2_length(n); if (r >= 0) s_ch[CH_OUT2].samples = n; return r; }
static int h_o2_out_b(int vol, void *buf) { mix_in(CH_OUT2, buf, vol, vol); return o_o2_out_b(vol, buf); }

static int s_hooked;

static void *find_audio(u32 nid)
{
    static const char *mods[] = {"sceAudio_Driver", "sceAudio_driver", "sceAudio"};
    for (unsigned i = 0; i < sizeof(mods) / sizeof(mods[0]); i++) {
        u32 f = sctrlHENFindFunction(mods[i], "sceAudio", nid);
        if (f)
            return (void *)f;
    }
    return NULL;
}

/* orig = the function; *save = where its address is kept; hook = ours */
static void hook(u32 nid, void **save, void *fn)
{
    void *orig = find_audio(nid);
    if (!orig)
        return;
    *save = orig;
    sctrlHENPatchSyscall(orig, fn);
    s_hooked++;
}

int audio_hooks_install(void)
{
    if (s_hooked)
        return s_hooked;
    hook(NID_RESERVE, (void **)&o_reserve, h_reserve);
    hook(NID_RELEASE, (void **)&o_release, h_release);
    hook(NID_DATALEN, (void **)&o_datalen, h_datalen);
    hook(NID_CONFIG, (void **)&o_config, h_config);
    hook(NID_OUT, (void **)&o_out, h_out);
    hook(NID_OUT_B, (void **)&o_out_b, h_out_b);
    hook(NID_PAN, (void **)&o_pan, h_pan);
    hook(NID_PAN_B, (void **)&o_pan_b, h_pan_b);
    hook(NID_SRC_RESERVE, (void **)&o_src_reserve, h_src_reserve);
    hook(NID_SRC_RELEASE, (void **)&o_src_release, h_src_release);
    hook(NID_SRC_OUT_B, (void **)&o_src_out_b, h_src_out_b);
    hook(NID_O2_RESERVE, (void **)&o_o2_reserve, h_o2_reserve);
    hook(NID_O2_RELEASE, (void **)&o_o2_release, h_o2_release);
    hook(NID_O2_LENGTH, (void **)&o_o2_length, h_o2_length);
    hook(NID_O2_OUT_B, (void **)&o_o2_out_b, h_o2_out_b);
    return s_hooked;
}

/* --------------------------------------------------------------- the mix */

int audio_start(void)
{
    if (s_mix)
        return 0;
    if (!s_hooked)
        return -1;
    int bytes = MIXN * 2 * 4 + AUDIO_TAKE_MAX * 4 + 64;
    s_block = sceKernelAllocPartitionMemory(1, "XLTCastAudio", PSP_SMEM_High, bytes, NULL);
    if (s_block < 0)
        return -1;
    int *mix = (int *)(((unsigned)sceKernelGetBlockHeadAddr(s_block) + 63) & ~63u);
    memset(mix, 0, MIXN * 2 * 4);
    s_out = (short *)(mix + MIXN * 2);
    s_rpos = now_frames();
    for (int c = 0; c < NCH; c++)
        s_ch[c].pos = 0;
    s_taken = s_blocks = 0;
    s_mix = mix; /* last: the hooks start mixing */
    return 0;
}

void audio_stop(void)
{
    unsigned st = pspSdkDisableInterrupts();
    s_mix = NULL; /* the hooks stop at once (they check it with interrupts off) */
    pspSdkEnableInterrupts(st);
    if (s_block >= 0)
        sceKernelFreePartitionMemory(s_block);
    s_block = -1;
    s_out = NULL;
}

int audio_take(const short **out, unsigned *first)
{
    if (!s_mix)
        return 0;
    u64 now = now_frames();
    if (s_rpos + MIXN / 2 < now) /* the sender fell far behind: skip ahead */
        s_rpos = now - MIXN / 8;
    *first = (unsigned)s_rpos;
    int n = 0;
    while (s_rpos < now && n < AUDIO_TAKE_MAX) {
        int chunk = 256;
        if ((u64)chunk > now - s_rpos) chunk = (int)(now - s_rpos);
        if (chunk > AUDIO_TAKE_MAX - n) chunk = AUDIO_TAKE_MAX - n;
        unsigned st = pspSdkDisableInterrupts();
        for (int i = 0; i < chunk; i++) {
            unsigned k = (unsigned)(s_rpos + i) & (MIXN - 1);
            int l = s_mix[2 * k], r = s_mix[2 * k + 1];
            s_mix[2 * k] = s_mix[2 * k + 1] = 0;
            s_out[2 * (n + i)] = l > 32767 ? 32767 : l < -32768 ? -32768 : l;
            s_out[2 * (n + i) + 1] = r > 32767 ? 32767 : r < -32768 ? -32768 : r;
        }
        s_rpos += chunk;
        pspSdkEnableInterrupts(st);
        n += chunk;
    }
    s_taken += n;
    *out = s_out;
    return n;
}

int audio_hooked(void) { return s_hooked; }
unsigned audio_blocks(void) { return s_blocks; }
