/* xltsync - the USB link between XLT and the PC. Shared by the kernel module
 * (main.c) and XLT (src/usbsync.c); tools/xltlink.py mirrors it (keep the two
 * in step).
 *
 * Every command is one 20-byte header in its own OUT transfer, then, if it
 * carries a payload, len bytes in the next transfer. Replies are the same
 * header with XLTS_REPLY set, then len bytes. Little-endian (the PSP's order).
 *
 * Paths are full PSP paths, "ms0:/VIDEO/a.mp4" (or ef0: on a PSP Go), sent
 * NUL-terminated. Anything may be listed; only XLT's folders may be changed
 * (see path_ok() in syncpath.c): ISO, VIDEO, MUSIC, PICTURE, BOOKS, DOWNLOADS and
 * PSP/GAME, never the folders themselves; and in PSP/SAVEDATA a save folder and
 * the files directly in it.
 *
 * Sending a file (PUT), in four steps:
 *   PC: PUT header + XltsPut       PSP: reply, arg = transfer size to use
 *   PC: size bytes, in transfers of that size (the last one shorter)
 *   PSP: reply with XltsPutDone (CRC-32 of what it wrote, time taken)
 *   PC: PUT_END, arg 1 = keep it (if the CRC matches), 0 = throw it away
 * Until PUT_END the file is "<path>.xltpart"; keeping it renames it. */
#ifndef XLTSYNC_H
#define XLTSYNC_H

#define XLTSYNC_DRIVER "XLTSyncDriver"
#define XLTSYNC_PID 0x01CB      /* with Sony's vendor ID 054C, which the bus driver sets */
#define XLTSYNC_BCD 0x0102      /* the device version: Windows caches the MS OS answers per version */
#define XLTSYNC_SUBCLASS 0x58   /* 'X': the vendor interface the PC side looks for */
#define XLTSYNC_PROTOCOL 0x53   /* 'S' */
#define XLTSYNC_VENDOR_REQ 0x58 /* Microsoft OS descriptor requests (string 0xEE's and the BOS's code) */

#define XLTS_MAGIC 0x53544C58u /* "XLTS" */
#define XLTS_PROTO 4           /* 1: files; 2: fast DF, DELETE_TREE, save data; 3: READ; 4: POWER */
#define XLTS_REPLY 0x8000
#define XLTS_PART ".xltpart"

enum {
    XLTS_HELLO = 1,   /* -> XltsHello */
    XLTS_ECHO = 2,    /* len bytes -> the same bytes */
    XLTS_SINK = 3,    /* len bytes in transfers of arg bytes -> reply arg = microseconds */
    XLTS_SOURCE = 4,  /* -> go-ahead reply, then len bytes in transfers of arg bytes */
    XLTS_CTLLOG = 5,  /* -> text: the control requests seen, reply arg = count */
    XLTS_DF = 6,      /* path (a device), arg 1 = count again -> XltsDf, reply arg = XLTS_DF_*.
                       * The driver's own count, then kept up to date with sync's own changes
                       * for XLTS_DF_KEEP_US. (XLTS_DF_COUNTED, from the FAT: older XLT only) */
    XLTS_LIST = 7,    /* path of a folder, arg = entries to skip -> XltsEntry..., arg = count */
    XLTS_STAT = 8,    /* path -> one XltsEntry */
    XLTS_MKDIR = 9,   /* path, with its parents */
    XLTS_PUT = 10,    /* XltsPut -> arg = transfer size; see above */
    XLTS_PUT_END = 11,/* arg 1 = keep, 0 = throw away */
    XLTS_DELETE = 12, /* path: a file, or an empty folder */
    XLTS_RENAME = 13, /* "from\0to\0" (the firmware may refuse between folders) */
    XLTS_GET = 14,    /* path, arg = transfer size -> reply arg = file size, then the file in
                       * transfers of that size, then a reply with arg = its CRC-32 */
    XLTS_DELETE_TREE = 15, /* path: a folder and everything in it (or a file) -> XltsRemoved.
                       * Not XLT's own folder. On an error, what was removed so far stays removed */
    XLTS_READ = 16,   /* XltsRead -> reply arg = bytes read, then those bytes (fewer at the file's end).
                       * A piece of a file, up to the buffer's size: a game's PARAM.SFO and icons
                       * inside an ISO or an EBOOT, without sending the whole file */
    XLTS_POWER = 17,  /* -> XltsPower: the battery, for XLT Tool's status line */
};

/* DF's reply arg: how the figure was got */
enum { XLTS_DF_COUNTED = 0, XLTS_DF_KEPT = 1, XLTS_DF_DRIVER = 2 };
#define XLTS_DF_KEEP_US 60000000u /* a count is used again for 60 s (XLT itself may write meanwhile) */

enum {
    XLTS_OK = 0,
    XLTS_E_OP = 1,    /* unknown command */
    XLTS_E_SIZE = 2,  /* a length or size out of range */
    XLTS_MORE = 3,    /* LIST: not all entries fit; ask again, skipping these */
    XLTS_E_PATH = 4,  /* not a valid path, or not one that may be changed */
    XLTS_E_NOENT = 5, /* not there */
    XLTS_E_EXIST = 6, /* already there (PUT without XLTS_PUT_REPLACE) */
    XLTS_E_IO = 7,    /* the firmware refused: reply arg = its error code */
    XLTS_E_FULL = 8,  /* the Memory Stick is full */
    XLTS_E_STATE = 9, /* PUT_END without a PUT waiting for it */
};

typedef struct {
    unsigned magic;
    unsigned short op, status;
    unsigned seq, len, arg;
} __attribute__((packed)) XltsHeader;

typedef struct {
    unsigned proto, buf_size, speed, model;
    char name[32];
    unsigned put_chunk; /* the transfer size PUT uses */
    unsigned devices;   /* 1 = ms0: there, 2 = ef0: there */
} __attribute__((packed)) XltsHello;

typedef struct {
    unsigned long long free_bytes, total_bytes;
} __attribute__((packed)) XltsDf;

typedef struct {
    unsigned files, folders;
    unsigned long long bytes;
} __attribute__((packed)) XltsRemoved;

/* A date as the PSP keeps it on FAT: local time. */
typedef struct {
    unsigned short year, month, day, hour, minute, second;
} __attribute__((packed)) XltsTime;

/* LIST / STAT: one after the other, each 18 + name_len bytes */
typedef struct {
    unsigned size;
    XltsTime mtime;
    unsigned char is_dir, name_len; /* the name follows, not NUL-terminated */
} __attribute__((packed)) XltsEntry;

#define XLTS_PUT_REPLACE 1 /* replace a file already there */

typedef struct {
    unsigned size, flags;
    XltsTime mtime; /* year 0: leave the PSP's own */
    /* the path follows, NUL-terminated */
} __attribute__((packed)) XltsPut;

typedef struct {
    unsigned crc32, microseconds, written;
} __attribute__((packed)) XltsPutDone;

typedef struct {
    int battery;      /* percent, -1 if there is no battery */
    int charging;     /* 1 while it charges */
    int plugged;      /* 1 on the charger (external power) */
    int minutes;      /* battery time left as the PSP reckons it, -1 if unknown */
} __attribute__((packed)) XltsPower;

typedef struct {
    unsigned long long offset;
    unsigned length;
    /* the path follows, NUL-terminated */
} __attribute__((packed)) XltsRead;

/* What XLT reads (src/usbsync.c): the module exports xltsyncStatus(XltsStatus *)
 * in its kernel library "XLTSync"; XLT finds it with sctrlHENFindFunction and
 * calls it through kubridge. NID = the first 4 bytes of SHA-1("xltsyncStatus"),
 * little-endian (check: python3 -c "import hashlib;print(hex(int.from_bytes(
 * hashlib.sha1(b'xltsyncStatus').digest()[:4],'little')))"). */
#define XLTSYNC_LIB "XLTSync"
#define XLTSYNC_NID_STATUS 0x00AAD55Au

typedef struct {
    unsigned attached; /* the PC has set the device up (cable in) */
    unsigned talking;  /* a PC program said something in the last few seconds, or a file is coming in */
    unsigned busy;     /* a file is coming in */
    unsigned changes;  /* counts up with every file kept, deleted or renamed, every folder made */
    unsigned done, total; /* the file coming in: bytes so far, its size */
    char name[64];     /* its name (the last part of its path, Shift-JIS) */
} XltsStatus;

#endif
