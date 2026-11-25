/* xltcast - the game's picture sent to the PC over USB (XLT Cast). Shared by the
 * kernel plugin (main.c) and mirrored by tools/xltcast.py (keep the two in step).
 *
 * The PC picks how frames are sent and reads counters (the frame rate on the PC, how
 * much casting slows the game: roadmap step C1). Installed by XLT (src/castplug.c).
 *
 * Every message is one 32-byte header in its own transfer, then, if len is not 0,
 * len bytes. Frames come in pieces of XLTC_PIECE_ROWS rows, one transfer each (all
 * whole 512-byte packets). Little-endian (the PSP's order).
 *
 * PC -> PSP: HELLO, START (a = mode, b = thread priority or 0, c = send every c-th
 * new frame, 0 or 1 = all, d = copy method, XLTC_M_*), STOP, STATS. Each gets a reply (op | XLTC_REPLY).
 * PSP -> PC while started: FRAME headers, each followed by the frame's pieces:
 *   a = pixel format of what is sent (PSP_DISPLAY_PIXEL_FORMAT_*), b = row length in
 *   pixels, c = the frame's number (frame buffer changes counted), d = microseconds
 *   the last frame took to send. Rows: XLTC_H. */
#ifndef XLTCAST_H
#define XLTCAST_H

#define XLTCAST_DRIVER "XLTCastDriver"
#define XLTCAST_PID 0x01CB      /* sync's: the same udev rule and WinUSB set-up */
#define XLTCAST_BCD 0x0201      /* not sync's 0x0102: Windows caches the descriptors per version */
#define XLTCAST_SUBCLASS 0x58   /* 'X' */
#define XLTCAST_PROTOCOL 0x43   /* 'C': sync is 'S' */
#define XLTCAST_VENDOR_REQ 0x58

#define XLTC_MAGIC 0x43544C58u /* "XLTC" */
#define XLTC_VERSION 3      /* 2: copy methods (START d), XltcStats.method; 3: AUDIO */
#define XLTC_REPLY 0x8000
#define XLTC_H 272
#define XLTC_PIECE_ROWS 64     /* 272 = 4 x 64 + 16 */

enum { XLTC_HELLO = 1, XLTC_START = 2, XLTC_STOP = 3, XLTC_STATS = 4, XLTC_FRAME = 0x10, XLTC_AUDIO = 0x11, XLTC_NOP = 0x12 };
/* NOP (PSP -> PC, PS1 games): nothing; ends each message there, as POPS holds the last transfer back. */
/* AUDIO (while started, between frames): a = sample rate (44100), b = the first sample's number on
 * the PSP's timeline; len bytes of 16-bit stereo follow in one transfer. */
enum {
    XLTC_DIRECT = 1, /* the USB controller reads the frame buffer itself (rows of the buffer's width) */
    XLTC_COPY = 2,   /* copied to RAM first, 480 wide; 32-bit colour turned into 565 */
};
/* how copy mode reads the frame buffer */
enum {
    XLTC_M_AUTO = 0,     /* the best there is: cached */
    XLTC_M_UNCACHED = 1, /* the CPU reads through the uncached address (version 1's way) */
    XLTC_M_CACHED = 2,   /* the CPU reads through the cache (its lines dropped first) */
    XLTC_M_DMA = 3,      /* removed: sceDmacMemcpy from the frame buffer froze the PSP (E1004, 2026-11-20); now cached */
    XLTC_M_GE = 4,       /* removed: a GE transfer job queued beside the game's froze the PSP (E1004, 2026-11-20); now cached */
};
enum { XLTC_OK = 0, XLTC_E_OP = 1, XLTC_E_MODE = 2, XLTC_E_MEM = 3 };

typedef struct {
    unsigned magic;
    unsigned short op, status;
    unsigned seq, len;
    unsigned a, b, c, d;
} XltcHeader;

/* STATS and HELLO's payload. Counters run from the plugin's start: the PC takes differences. */
typedef struct {
    unsigned version;
    unsigned flips;     /* frame buffer changes seen (the game's frames) */
    unsigned vblanks;   /* vertical blanks seen (60 a second) */
    unsigned sent;      /* frames sent */
    unsigned failed;    /* frames whose sending failed */
    unsigned send_us;   /* time spent sending, all frames */
    unsigned grab_us;   /* of which copying and converting (copy mode) */
    unsigned fmt;       /* the game's pixel format now */
    unsigned stride;    /* its frame buffer's width in pixels */
    unsigned mode;      /* 0 stopped, else XLTC_DIRECT / XLTC_COPY */
    unsigned speed;     /* 1 full speed, 2 high speed */
    unsigned copy_buf;  /* bytes of RAM the copy mode has (0: copy mode can't run) */
    unsigned prio;      /* the sending thread's priority */
    unsigned last_err;  /* the last failed transfer's code */
    unsigned method;    /* the copy method the last frame used (XLTC_M_*) */
    unsigned dma_err;   /* frames dropped: the game showed another while one was copied (it drew over it) */
    unsigned audio_hooks;  /* sceAudio calls hooked (15: all) */
    unsigned audio_frames; /* stereo samples sent */
    unsigned ge_waits;     /* frames whose copy waited for the GE to finish drawing */
    unsigned ge_wait_us;   /* time spent waiting so */
    unsigned ge_timeouts;  /* frames the GE still drew after GE_IDLE_WAIT_US (copied anyway) */
} XltcStats;

#endif
