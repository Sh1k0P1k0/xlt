/* xltsync - XLT's own USB device, for sending files from the PC while XLT runs.
 *
 * The PSP shows up as a vendor device (Sony's vendor ID, product XLTSYNC_PID)
 * with one bulk endpoint each way. The PC lists folders and sends, renames and
 * deletes files (protocol: xltsync.h; PC side: tools/xltlink.py). Unlike the
 * USB mass storage mode, the Memory Stick stays XLT's the whole time.
 *
 * Windows: the device answers Microsoft's OS descriptor requests (string 0xEE,
 * then the compatible ID "WINUSB" and an interface GUID), so Windows binds its
 * own WinUSB driver without anything to install.
 *
 * XLT loads it (src/usbsync.c) after the system's usb.prx, then starts it with
 * sceUsbStart(XLTSYNC_DRIVER) and sceUsbActivate(XLTSYNC_PID) like the mass
 * storage driver. Everything is logged to the file XLT passes at start.
 *
 * The bus driver structures and the request flow follow PSPLINK's usbhostfs
 * (BSD licence, (c) 2006 James F and the pspdev contributors). */
#include <pspkernel.h>
#include <pspsysmem_kernel.h>
#include <pspiofilemgr.h>
#include <pspusb.h>
#include <pspusbbus.h>
#include <stddef.h>
#include <string.h>
/* from the kernel's SysclibForKernel (libpspsysclib_kernel_660), which the SDK doesn't declare */
int snprintf(char *buf, size_t n, const char *fmt, ...);
#include "xltsync.h"
#include "syncpath.h"
#include <systemctrl.h>
#include "../../src/fatcount.h"

PSP_MODULE_INFO("XLTSync", PSP_MODULE_KERNEL, 1, 1);

/* ------------------------------------------------------------------ log */

static char s_log[128];

static void log_line(const char *line)
{
    if (!s_log[0])
        return;
    SceUID fd = sceIoOpen(s_log, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd < 0)
        return;
    sceIoWrite(fd, line, strlen(line));
    sceIoWrite(fd, "\n", 1);
    sceIoClose(fd);
}
/* (the kernel's C library has snprintf but no vsnprintf) */
/* each line starts with the time in ms (sceKernelGetSystemTimeLow, the clock XLT's notes use too) */
#define LOG(...) do { char l_[320]; int n_ = snprintf(l_, sizeof(l_), "%u ", sceKernelGetSystemTimeLow() / 1000); \
                      snprintf(l_ + n_, sizeof(l_) - n_, __VA_ARGS__); log_line(l_); } while (0)

/* ---------------------------------------------------------------- cache */

/* The USB controller reads and writes RAM itself: the CPU's data cache must hold
 * nothing of a buffer while it does. Allegrex: cache 0x1B = hit writeback
 * invalidate, 64-byte lines. (Done here, not with UtilsForKernel, whose 6.60
 * NIDs the SDK doesn't carry.) */
static void dcache_flush(const void *p, int n)
{
    unsigned a = (unsigned)p & ~63u, end = ((unsigned)p + n + 63) & ~63u;
    for (; a < end; a += 64)
        __asm__ volatile("cache 0x1B, 0(%0)" : : "r"(a) : "memory");
    __asm__ volatile("sync" : : : "memory");
}

/* --------------------------------------------------------------- crc32 */

/* slice-by-4: four bytes a step, a quarter of the table lookups' dependencies */
static unsigned s_crc[4][256];

static void crc_init(void)
{
    for (unsigned i = 0; i < 256; i++) {
        unsigned c = i;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        s_crc[0][i] = c;
    }
    for (unsigned i = 0; i < 256; i++)
        for (int t = 1; t < 4; t++)
            s_crc[t][i] = (s_crc[t - 1][i] >> 8) ^ s_crc[0][s_crc[t - 1][i] & 0xFF];
}

/* as zlib's crc32(): start with 0, feed the result back in */
static unsigned crc_update(unsigned crc, const unsigned char *p, unsigned n)
{
    crc = ~crc;
    while (n && ((unsigned)p & 3)) {
        crc = s_crc[0][(crc ^ *p++) & 0xFF] ^ (crc >> 8);
        n--;
    }
    for (; n >= 4; n -= 4, p += 4) {
        crc ^= *(const unsigned *)p; /* little-endian, as the PSP */
        crc = s_crc[3][crc & 0xFF] ^ s_crc[2][(crc >> 8) & 0xFF] ^ s_crc[1][(crc >> 16) & 0xFF] ^ s_crc[0][crc >> 24];
    }
    while (n--)
        crc = s_crc[0][(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

/* ---------------------------------------------------------- descriptors */

#define EP_IN 0x81
#define EP_OUT 0x02

static struct DeviceDescriptor s_dev_hi = {
    .bLength = 18, .bDescriptorType = 0x01, .bcdUSB = 0x201, /* 2.01: a BOS descriptor (MS OS 2.0) */
    .bDeviceClass = 0, .bDeviceSubClass = 0, .bDeviceProtocol = 0, .bMaxPacketSize = 64,
    .idVendor = 0, .idProduct = 0, /* the bus driver fills in Sony's and sceUsbActivate's */
    .bcdDevice = XLTSYNC_BCD, .iManufacturer = 0, .iProduct = 0, .iSerialNumber = 0,
    .bNumConfigurations = 1,
};
static struct ConfigDescriptor s_conf_hi = {
    .bLength = 9, .bDescriptorType = 2, .wTotalLength = 9 + 9 + 2 * 7, .bNumInterfaces = 1,
    .bConfigurationValue = 1, .iConfiguration = 0, .bmAttributes = 0xC0, .bMaxPower = 0,
};
static struct InterfaceDescriptor s_inter_hi = {
    .bLength = 9, .bDescriptorType = 4, .bInterfaceNumber = 0, .bAlternateSetting = 0,
    .bNumEndpoints = 2, .bInterfaceClass = 0xFF, .bInterfaceSubClass = XLTSYNC_SUBCLASS,
    .bInterfaceProtocol = XLTSYNC_PROTOCOL, .iInterface = 1,
};
static struct EndpointDescriptor s_endp_hi[2] = {
    {.bLength = 7, .bDescriptorType = 5, .bEndpointAddress = EP_IN, .bmAttributes = 2, .wMaxPacketSize = 512, .bInterval = 0},
    {.bLength = 7, .bDescriptorType = 5, .bEndpointAddress = EP_OUT, .bmAttributes = 2, .wMaxPacketSize = 512, .bInterval = 0},
};
/* full speed (USB 1.1 ports and hubs): the same with 64-byte packets */
static struct DeviceDescriptor s_dev_full;
static struct ConfigDescriptor s_conf_full;
static struct InterfaceDescriptor s_inter_full;
static struct EndpointDescriptor s_endp_full[2];

/* the one string the bus driver takes: the interface's name (UTF-16) */
static unsigned char s_str[] = {18, 3, 'X', 0, 'L', 0, 'T', 0, ' ', 0, 'S', 0, 'y', 0, 'n', 0, 'c', 0};

static struct UsbEndpoint s_ep[3] = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
static struct UsbInterface s_intp = {0xFFFFFFFF, 0, 1};
static struct UsbData s_data[2];

/* Microsoft OS descriptors 1.0 (Windows 7 and later, all of them still):
 * string 0xEE says which vendor request to ask, which then gives the
 * compatible ID (WINUSB) and the interface GUID programs find the device by. */
static const unsigned char s_ms_string[18] = {
    18, 3, 'M', 0, 'S', 0, 'F', 0, 'T', 0, '1', 0, '0', 0, '0', 0, XLTSYNC_VENDOR_REQ, 0,
};
static const unsigned char s_ms_compat[40] = {
    40, 0, 0, 0, 0x00, 0x01, 4, 0, 1, 0, 0, 0, 0, 0, 0, 0, /* length, version 1.0, index 4, 1 function */
    0, 1, 'W', 'I', 'N', 'U', 'S', 'B', 0, 0,              /* interface 0, compatible ID */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,              /* sub-compatible ID, reserved */
};
#define XLTSYNC_GUID "{AE1513B4-0AA3-4365-B951-3261C67CE994}"
static unsigned char s_ms_props[142]; /* built at start: UTF-16 is long-hand */

static void build_ms_props(void)
{
    static const char name[] = "DeviceInterfaceGUID", guid[] = XLTSYNC_GUID;
    unsigned char *p = s_ms_props;
    unsigned name_len = (sizeof(name)) * 2, data_len = (sizeof(guid)) * 2; /* with the NUL */
    unsigned prop = 4 + 4 + 2 + name_len + 4 + data_len, total = 10 + prop;
#define PUT32(v) do { unsigned v_ = (v); *p++ = v_; *p++ = v_ >> 8; *p++ = v_ >> 16; *p++ = v_ >> 24; } while (0)
#define PUT16(v) do { unsigned v_ = (v); *p++ = v_; *p++ = v_ >> 8; } while (0)
    PUT32(total);
    PUT16(0x0100);
    PUT16(5);
    PUT16(1);
    PUT32(prop);
    PUT32(1); /* REG_SZ */
    PUT16(name_len);
    for (unsigned i = 0; i < sizeof(name); i++) PUT16((unsigned char)name[i]);
    PUT32(data_len);
    for (unsigned i = 0; i < sizeof(guid); i++) PUT16((unsigned char)guid[i]);
#undef PUT32
#undef PUT16
}

/* Microsoft OS descriptors 2.0 (Windows 8.1 and later): the BOS descriptor names
 * the vendor request that returns the descriptor set: WINUSB and the interface
 * GUID together. Both are device requests, which the PSP's bus driver passes
 * on; its own handling of interface requests refuses MS OS 1.0's GUID request
 * (bmRequestType 0xC1), so 1.0 gets Windows only the driver, not the GUID. */
#define MSOS20_SET_LEN (10 + 20 + 132)
static unsigned char s_bos[5 + 28];
static unsigned char s_msos20[MSOS20_SET_LEN];

static void build_msos20(void)
{
    static const unsigned char uuid[16] = {0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
                                           0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F};
    static const char name[] = "DeviceInterfaceGUIDs", guid[] = XLTSYNC_GUID;
    unsigned char *p = s_bos;
#define PUT32(v) do { unsigned v_ = (v); *p++ = v_; *p++ = v_ >> 8; *p++ = v_ >> 16; *p++ = v_ >> 24; } while (0)
#define PUT16(v) do { unsigned v_ = (v); *p++ = v_; *p++ = v_ >> 8; } while (0)
    *p++ = 5; *p++ = 0x0F; PUT16(sizeof(s_bos)); *p++ = 1;           /* BOS: one capability */
    *p++ = 28; *p++ = 0x10; *p++ = 0x05; *p++ = 0;                  /* platform capability */
    memcpy(p, uuid, 16); p += 16;
    PUT32(0x06030000); PUT16(MSOS20_SET_LEN); *p++ = XLTSYNC_VENDOR_REQ; *p++ = 0;

    p = s_msos20;
    PUT16(10); PUT16(0x00); PUT32(0x06030000); PUT16(MSOS20_SET_LEN); /* set header */
    PUT16(20); PUT16(0x03);                                           /* compatible ID */
    memcpy(p, "WINUSB\0\0", 8); p += 8;
    memset(p, 0, 8); p += 8;
    PUT16(132); PUT16(0x04); PUT16(7);                                /* registry property, REG_MULTI_SZ */
    PUT16(sizeof(name) * 2);
    for (unsigned i = 0; i < sizeof(name); i++) PUT16((unsigned char)name[i]);
    PUT16((sizeof(guid) + 1) * 2);                                    /* the GUID, then the list's end */
    for (unsigned i = 0; i < sizeof(guid); i++) PUT16((unsigned char)guid[i]);
    PUT16(0);
#undef PUT32
#undef PUT16
}

/* ---------------------------------------------------------------- state */

enum {
    EV_ATTACH = 1, EV_DETACH = 2, EV_IN_DONE = 4, EV_OUT_DONE = 8, EV_QUIT = 16, EV_CTL_DONE = 32,
};
static SceUID s_ev = -1, s_thread = -1;
static volatile int s_speed;         /* 0 not attached, 1 full speed, 2 high speed */
static struct UsbdDeviceReq s_in_req, s_out_req, s_ctl_req;

/* Transfers go through one buffer: a big one from the kernel's memory if there is
 * room, else the small static one. Both 64-byte aligned. PUT uses it as two
 * halves: one fills from USB while the other is written to the Memory Stick. */
static unsigned char s_small[16 * 1024] __attribute__((aligned(64)));
static unsigned char *s_buf = s_small;
static int s_buf_size = sizeof(s_small);
static SceUID s_buf_block = -1;

/* Control requests the bus driver passes on, kept for CTLLOG and the log.
 * Written in the bus driver's callback, read by the thread. */
#define CTL_MAX 32
static struct CtlEntry {
    unsigned char type, req;
    unsigned short value, index, length;
    int answered; /* 0 answered, 1 left to the bus driver, 2 stalled, <0 the send failed */
} s_ctl[CTL_MAX];
static volatile int s_ctl_count;
static unsigned char s_ctl_reply[192] __attribute__((aligned(64)));

/* For XLT (xltsyncStatus): plain words, written by the thread, read whenever. */
static volatile unsigned s_talking, s_busy, s_changes, s_done, s_total, s_last_cmd;
#define TALKING_US 5000000 /* the PC tool ends without a word: a while without commands is "not connected" */
static char s_name[64];

int xltsyncStatus(XltsStatus *out)
{
    out->attached = s_speed != 0;
    out->talking = s_busy || (s_talking && sceKernelGetSystemTimeLow() - s_last_cmd < TALKING_US);
    out->busy = s_busy;
    out->changes = s_changes;
    out->done = s_done;
    out->total = s_total;
    memcpy(out->name, s_name, sizeof(out->name));
    return 0;
}

/* --------------------------------------------------------- bus callbacks */

static int cb_in_done(struct UsbdDeviceReq *r, int a2, int a3) { sceKernelSetEventFlag(s_ev, EV_IN_DONE); return 0; }
static int cb_out_done(struct UsbdDeviceReq *r, int a2, int a3) { sceKernelSetEventFlag(s_ev, EV_OUT_DONE); return 0; }
static int cb_ctl_done(struct UsbdDeviceReq *r, int a2, int a3) { sceKernelSetEventFlag(s_ev, EV_CTL_DONE); return 0; }

static int ep0_send(const void *data, int size, int wlength)
{
    int n = size < wlength ? size : wlength;
    memcpy(s_ctl_reply, data, n);
    dcache_flush(s_ctl_reply, sizeof(s_ctl_reply));
    memset(&s_ctl_req, 0, sizeof(s_ctl_req));
    s_ctl_req.endp = &s_ep[0];
    s_ctl_req.data = s_ctl_reply;
    s_ctl_req.size = n;
    s_ctl_req.func = cb_ctl_done;
    return sceUsbbdReqSend(&s_ctl_req);
}

/* A control request the bus driver passes on. It answers the standard ones itself
 * (string 1 is its "Sony"), so only these are ours: Microsoft's string 0xEE, the
 * BOS descriptor and our vendor requests. Others are left alone; a vendor
 * request that isn't ours gets -1 back, which (to be confirmed) has the bus
 * driver refuse it: sceUsbbdStall on endpoint 0 didn't, the PC timed out. */
static int cb_control(int a1, int a2, struct DeviceRequest *req)
{
    int answered = 1, ret = 0;
    unsigned type = req->bmRequestType, desc = req->wValue >> 8;
    int ours = req->bRequest == XLTSYNC_VENDOR_REQ && (type == 0xC0 || type == 0xC1);
    if (type == 0x80 && req->bRequest == 6 && req->wValue == 0x03EE) {
        answered = ep0_send(s_ms_string, sizeof(s_ms_string), req->wLength);
    } else if (type == 0x80 && req->bRequest == 6 && desc == 0x0F) {
        answered = ep0_send(s_bos, sizeof(s_bos), req->wLength);
    } else if (ours && req->wIndex == 4) {
        answered = ep0_send(s_ms_compat, sizeof(s_ms_compat), req->wLength);
    } else if (ours && req->wIndex == 5) {
        answered = ep0_send(s_ms_props, sizeof(s_ms_props), req->wLength);
    } else if (ours && req->wIndex == 7) {
        answered = ep0_send(s_msos20, sizeof(s_msos20), req->wLength);
    } else if ((type & 0x60) == 0x40) {
        answered = 2;
        ret = -1;
    }
    int i = s_ctl_count;
    if (i < CTL_MAX) {
        s_ctl[i].type = req->bmRequestType;
        s_ctl[i].req = req->bRequest;
        s_ctl[i].value = req->wValue;
        s_ctl[i].index = req->wIndex;
        s_ctl[i].length = req->wLength;
        s_ctl[i].answered = answered;
        s_ctl_count = i + 1;
    }
    return ret;
}

static int cb_func28(int a1, int a2, int a3) { return 0; }

static int cb_attach(int speed, void *a2, void *a3)
{
    s_speed = speed;
    sceKernelSetEventFlag(s_ev, EV_ATTACH);
    return 0;
}

static int cb_detach(int a1, int a2, int a3)
{
    s_speed = 0;
    sceKernelSetEventFlag(s_ev, EV_DETACH);
    return 0;
}

/* --------------------------------------------------------------- transfers */

#define T_IDLE  0         /* waiting for the next command: no time limit */
#define T_DATA  3000000   /* in the middle of a command: 3 s without progress is a lost PC */

/* Waits for a transfer; -1 if the cable went, the module is stopping, or it timed out. */
static int wait_done(unsigned bit, struct UsbdDeviceReq *r, SceUInt timeout)
{
    u32 got = 0;
    SceUInt t = timeout;
    int ret = sceKernelWaitEventFlag(s_ev, bit | EV_DETACH | EV_QUIT, PSP_EVENT_WAITOR, &got, timeout ? &t : NULL);
    if (ret < 0 || !(got & bit)) {
        sceUsbbdReqCancelAll(r->endp);
        if (ret < 0) LOG("wait %x: %08x", bit, (unsigned)ret);
        return -1;
    }
    sceKernelClearEventFlag(s_ev, ~bit);
    if (r->retcode != 0) {
        LOG("transfer %x: retcode %d, %d bytes", bit, r->retcode, r->recvsize);
        return -1;
    }
    return r->recvsize;
}

/* Queues one OUT transfer (PC to PSP) into p, up to max bytes (a multiple of the
 * packet size, or the PC's transfer must be shorter). */
static int recv_queue(void *p, int max)
{
    dcache_flush(p, max);
    memset(&s_out_req, 0, sizeof(s_out_req));
    s_out_req.endp = &s_ep[2];
    s_out_req.data = p;
    s_out_req.size = max;
    s_out_req.func = cb_out_done;
    sceKernelClearEventFlag(s_ev, ~EV_OUT_DONE);
    int r = sceUsbbdReqRecv(&s_out_req);
    if (r < 0)
        LOG("ReqRecv: %08x", (unsigned)r);
    return r < 0 ? -1 : 0;
}

/* Waits for the queued OUT transfer: bytes received, -1 on error. */
static int recv_wait(SceUInt timeout)
{
    int r = wait_done(EV_OUT_DONE, &s_out_req, timeout);
    if (r > 0)
        dcache_flush(s_out_req.data, s_out_req.size); /* the CPU reads what the controller wrote */
    return r;
}

static int recv_into(void *p, int max, SceUInt timeout)
{
    return recv_queue(p, max) < 0 ? -1 : recv_wait(timeout);
}

static unsigned round_packet(unsigned n) { return (n + 511) & ~511u; }

/* One IN transfer (PSP to PC) of n bytes. */
static int send_from(const void *p, int n)
{
    dcache_flush(p, n);
    memset(&s_in_req, 0, sizeof(s_in_req));
    s_in_req.endp = &s_ep[1];
    s_in_req.data = (void *)p;
    s_in_req.size = n;
    s_in_req.func = cb_in_done;
    sceKernelClearEventFlag(s_ev, ~EV_IN_DONE);
    int r = sceUsbbdReqSend(&s_in_req);
    if (r < 0) {
        LOG("ReqSend: %08x", (unsigned)r);
        return -1;
    }
    r = wait_done(EV_IN_DONE, &s_in_req, T_DATA);
    return r == n ? n : -1;
}

/* A reply: the header, then (in its own transfer) len bytes from the buffer. */
static int reply(const XltsHeader *cmd, int status, unsigned len, unsigned arg)
{
    static XltsHeader h __attribute__((aligned(64)));
    h.magic = XLTS_MAGIC;
    h.op = cmd->op | XLTS_REPLY;
    h.status = status;
    h.seq = cmd->seq;
    h.len = len;
    h.arg = arg;
    if (send_from(&h, sizeof(h)) < 0)
        return -1;
    return len ? (send_from(s_buf, len) < 0 ? -1 : 0) : 0;
}

/* A command's payload into the buffer, NUL-terminated (paths). -1 on a lost link. */
static int recv_payload(const XltsHeader *c)
{
    if (!c->len)
        return 0;
    if (recv_into(s_buf, round_packet(c->len), T_DATA) != (int)c->len)
        return -1;
    s_buf[c->len] = 0;
    return 0;
}

/* ----------------------------------------------------------------- paths */

/* path_ok() and the rules for which paths may be changed: syncpath.c */

/* Makes every folder on the way to path (and path itself if dir_too). */
static void make_dirs(const char *path, int dir_too)
{
    char p[256];
    snprintf(p, sizeof(p), "%s", path);
    for (char *q = p + 5; *q; q++) {
        if (*q == '/') {
            *q = 0;
            sceIoMkdir(p, 0777);
            *q = '/';
        }
    }
    if (dir_too)
        sceIoMkdir(p, 0777);
}

static int io_status(int err)
{
    if (err == (int)0x80010002) return XLTS_E_NOENT; /* SCE_ERROR_ERRNO_ENOENT */
    if (err == (int)0x80010011) return XLTS_E_EXIST; /* EEXIST */
    if (err == (int)0x8001001C) return XLTS_E_FULL;  /* ENOSPC */
    return XLTS_E_IO;
}

/* ---------------------------------------------------------------- commands */

static int dev_present(const char *dev)
{
    SceUID d = sceIoDopen(dev);
    if (d < 0)
        return 0;
    sceIoDclose(d);
    return 1;
}

static int cmd_hello(const XltsHeader *c)
{
    XltsHello *h = (XltsHello *)s_buf;
    memset(h, 0, sizeof(*h));
    h->proto = XLTS_PROTO;
    h->buf_size = s_buf_size;
    h->speed = s_speed;
    h->model = sceKernelGetModel();
    snprintf(h->name, sizeof(h->name), "XLT Sync");
    h->put_chunk = s_buf_size / 2;
    h->devices = (dev_present("ms0:/") ? 1 : 0) | (dev_present("ef0:/") ? 2 : 0);
    s_talking = 1;
    return reply(c, 0, sizeof(*h), 0);
}

static int cmd_echo(const XltsHeader *c)
{
    if (c->len > (unsigned)s_buf_size)
        return reply(c, XLTS_E_SIZE, 0, s_buf_size);
    /* exactly the length, rounded up to whole packets: a short last packet ends the
     * transfer, and one of whole packets ends by size (no zero-length packet needed) */
    if (c->len && recv_into(s_buf, round_packet(c->len), T_DATA) != (int)c->len)
        return -1;
    return reply(c, 0, c->len, 0);
}

/* SINK: the PC sends len bytes in transfers of arg bytes; the PSP only counts them. */
static int cmd_sink(const XltsHeader *c)
{
    unsigned chunk = c->arg ? c->arg : (unsigned)s_buf_size;
    if (chunk > (unsigned)s_buf_size || chunk % 512)
        return reply(c, XLTS_E_SIZE, 0, s_buf_size);
    unsigned t0 = sceKernelGetSystemTimeLow(), got = 0;
    while (got < c->len) {
        unsigned want = c->len - got < chunk ? c->len - got : chunk;
        int n = recv_into(s_buf, round_packet(want), T_DATA);
        if (n <= 0)
            return -1;
        got += n;
    }
    unsigned us = sceKernelGetSystemTimeLow() - t0;
    LOG("sink: %u bytes in %u us, chunks of %u", got, us, chunk);
    return reply(c, got == c->len ? 0 : XLTS_E_SIZE, 0, us);
}

/* SOURCE: the PSP sends len bytes in transfers of arg bytes (a counting pattern). */
static int cmd_source(const XltsHeader *c)
{
    unsigned chunk = c->arg ? c->arg : (unsigned)s_buf_size;
    if (chunk > (unsigned)s_buf_size || chunk % 512)
        return reply(c, XLTS_E_SIZE, 0, s_buf_size);
    if (reply(c, 0, 0, 0) < 0) /* go-ahead first: then only data */
        return -1;
    for (unsigned i = 0; i < chunk; i += 4)
        *(unsigned *)(s_buf + i) = i;
    unsigned t0 = sceKernelGetSystemTimeLow(), sent = 0;
    while (sent < c->len) {
        unsigned n = c->len - sent < chunk ? c->len - sent : chunk;
        if (send_from(s_buf, n) < 0)
            return -1;
        sent += n;
    }
    unsigned us = sceKernelGetSystemTimeLow() - t0;
    LOG("source: %u bytes in %u us, chunks of %u", sent, us, chunk);
    return 0;
}

static int cmd_ctllog(const XltsHeader *c)
{
    int n = s_ctl_count, len = 0;
    for (int i = 0; i < n && len + 64 < s_buf_size; i++) {
        struct CtlEntry *e = &s_ctl[i];
        len += snprintf((char *)s_buf + len, s_buf_size - len, "%02x %02x %04x %04x %04x %d\n",
                        e->type, e->req, e->value, e->index, e->length, e->answered);
    }
    return reply(c, 0, len, n);
}

/* Free space. The driver's own count (devctl 0x02425818) walks the whole FAT inside
 * the driver and holds the PSP for ~12 s on a 128 GB card. Reading the FAT from the
 * raw partition and counting it here (src/fatcount.c, as XLT's About does) takes
 * about 1 s. The count is then kept for a while and moved with sync's own changes,
 * so the PC can ask before every file. Per device: 0 ms0:, 1 ef0:. */
static struct {
    int valid;
    unsigned at;                     /* when it was counted (system time, us) */
    unsigned cluster;                /* bytes per cluster */
    long long free_clusters;         /* may run ahead of the truth by a little: kept >= 0 when shown */
    unsigned long long total_bytes;
} s_space[2];

static int dev_index(const char *path) { return !strncmp(path, "ef0:", 4) ? 1 : 0; }

/* sync wrote (bytes > 0) or freed (bytes < 0) a file of that size on path's device */
static void space_moved(const char *path, long long bytes)
{
    int i = dev_index(path);
    if (!s_space[i].valid || !s_space[i].cluster || !bytes)
        return;
    /* unsigned: the kernel's library has __udivdi3, not the signed one */
    unsigned long long mag = bytes < 0 ? (unsigned long long)-bytes : (unsigned long long)bytes;
    long long n = (long long)((mag + s_space[i].cluster - 1) / s_space[i].cluster);
    s_space[i].free_clusters += bytes < 0 ? n : -n;
}

/* Counts ms0:'s free clusters from its FAT: 0, or -1 if the partition can't be read
 * (then the driver's count is used). Uses s_buf, free between commands. */
static int fat_count(unsigned *cluster, unsigned *free_n, unsigned *clusters)
{
    FatGeom g;
    unsigned part = 0;
    SceUID fd = sceIoOpen("msstor0p1:", PSP_O_RDONLY, 0);
    int ok = fd >= 0 && sceIoRead(fd, s_buf, 512) == 512 && fat_parse_boot(s_buf, &g) == 0;
    if (!ok) {
        /* the whole stick, through its MBR */
        if (fd >= 0)
            sceIoClose(fd);
        fd = sceIoOpen("msstor:", PSP_O_RDONLY, 0);
        if (fd < 0 || sceIoRead(fd, s_buf, 512) != 512) {
            if (fd >= 0) sceIoClose(fd);
            return -1;
        }
        long long lba = fat_mbr_first(s_buf);
        if (lba <= 0 || lba >= 0x400000 || sceIoLseek32(fd, (int)(lba * 512), PSP_SEEK_SET) < 0 ||
            sceIoRead(fd, s_buf, 512) != 512 || fat_parse_boot(s_buf, &g) != 0) {
            sceIoClose(fd);
            return -1;
        }
        part = (unsigned)lba * 512;
    }
    /* whole sectors only: a block device reads nothing else */
    unsigned start = g.fat_offset / g.sector * g.sector, skip = g.fat_offset - start;
    unsigned want = skip + g.fat_bytes, done = 0, n_free = 0;
    unsigned chunk = (unsigned)s_buf_size / g.sector * g.sector;
    int r = sceIoLseek32(fd, (int)(part + start), PSP_SEEK_SET) < 0 ? -1 : 0;
    while (r == 0 && done < want) {
        unsigned n = want - done < chunk ? (want - done + g.sector - 1) / g.sector * g.sector : chunk;
        if (sceIoRead(fd, s_buf, n) < (int)n) {
            r = -1;
            break;
        }
        unsigned lo = done < skip ? skip - done : 0; /* bytes of this chunk before the FAT */
        unsigned at = done + lo - skip, len = n - lo;
        if (at + len > g.fat_bytes)
            len = g.fat_bytes - at;
        n_free += fat_count_free(&g, s_buf + lo, len, at);
        done += n;
    }
    sceIoClose(fd);
    if (r < 0)
        return -1;
    *cluster = g.cluster;
    *free_n = n_free;
    *clusters = g.clusters;
    return 0;
}

static int cmd_df(const XltsHeader *c)
{
    if (c->len > 256 || recv_payload(c) < 0)
        return c->len > 256 ? reply(c, XLTS_E_SIZE, 0, 0) : -1;
    char dev[8];
    snprintf(dev, sizeof(dev), "%.4s", (char *)s_buf);
    if (strcmp(dev, "ms0:") && strcmp(dev, "ef0:"))
        return reply(c, XLTS_E_PATH, 0, 0);
    int i = dev_index(dev), how = XLTS_DF_KEPT;
    unsigned now = sceKernelGetSystemTimeLow();
    if (c->arg & 1 || !s_space[i].valid || now - s_space[i].at > XLTS_DF_KEEP_US) {
        unsigned cluster = 0, free_n = 0, clusters = 0, t0 = now;
        how = XLTS_DF_COUNTED;
        /* ms0: from its FAT; a PSP Go's internal storage only through the driver */
        if (i != 0 || fat_count(&cluster, &free_n, &clusters) < 0) {
            struct { u32 max_clusters, free_clusters, max_sectors, sector_size, sectors_per_cluster; } info;
            void *p = &info;
            memset(&info, 0, sizeof(info));
            int r = sceIoDevctl(dev, 0x02425818, &p, sizeof(p), NULL, 0);
            if (r < 0)
                return reply(c, io_status(r), 0, r);
            cluster = info.sector_size * info.sectors_per_cluster;
            free_n = info.free_clusters;
            clusters = info.max_clusters;
            how = XLTS_DF_DRIVER;
        }
        s_space[i].valid = 1;
        s_space[i].at = sceKernelGetSystemTimeLow();
        s_space[i].cluster = cluster;
        s_space[i].free_clusters = free_n;
        s_space[i].total_bytes = (unsigned long long)cluster * clusters;
        LOG("df %s: %u of %u clusters of %u bytes free, %s, %u ms", dev, free_n, clusters, cluster,
            how == XLTS_DF_DRIVER ? "the driver's count" : "from the FAT", (s_space[i].at - t0) / 1000);
    }
    XltsDf *d = (XltsDf *)s_buf;
    long long fc = s_space[i].free_clusters < 0 ? 0 : s_space[i].free_clusters;
    d->free_bytes = (unsigned long long)fc * s_space[i].cluster;
    d->total_bytes = s_space[i].total_bytes;
    return reply(c, 0, sizeof(*d), how);
}

static void entry_time(XltsTime *t, const ScePspDateTime *d)
{
    t->year = d->year;
    t->month = d->month;
    t->day = d->day;
    t->hour = d->hour;
    t->minute = d->minute;
    t->second = d->second;
}

/* one entry at out (room bytes free): its length, 0 if it doesn't fit */
static int put_entry(unsigned char *out, int room, const char *name, const SceIoStat *st)
{
    int n = strlen(name);
    if (n > 255)
        n = 255;
    if ((int)sizeof(XltsEntry) + n > room)
        return 0;
    XltsEntry e;
    e.size = FIO_S_ISDIR(st->st_mode) ? 0 : (unsigned)st->st_size;
    entry_time(&e.mtime, &st->sce_st_mtime);
    e.is_dir = FIO_S_ISDIR(st->st_mode) ? 1 : 0;
    e.name_len = n;
    memcpy(out, &e, sizeof(e));
    memcpy(out + sizeof(e), name, n);
    return sizeof(e) + n;
}

static char s_path[256], s_path2[256];


/* the payload as a path into s_path; the status to reply, 0 if fine */
static int take_path(const XltsHeader *c, int mode)
{
    if (c->len < 2 || c->len > sizeof(s_path))
        return XLTS_E_PATH;
    if (s_buf[c->len - 1] != 0)
        return XLTS_E_PATH;
    snprintf(s_path, sizeof(s_path), "%s", (char *)s_buf);
    return path_ok(s_path, mode) ? 0 : XLTS_E_PATH;
}

static int cmd_list(const XltsHeader *c)
{
    if (c->len > s_buf_size - 1 || recv_payload(c) < 0)
        return c->len > (unsigned)s_buf_size - 1 ? reply(c, XLTS_E_SIZE, 0, 0) : -1;
    int st = take_path(c, P_READ);
    if (st)
        return reply(c, st, 0, 0);
    SceUID d = sceIoDopen(s_path);
    if (d < 0)
        return reply(c, io_status(d), 0, d);
    static SceIoDirent e;
    int len = 0, count = 0, index = 0, status = 0;
    for (;;) {
        memset(&e, 0, sizeof(e));
        if (sceIoDread(d, &e) <= 0)
            break;
        if (!strcmp(e.d_name, ".") || !strcmp(e.d_name, ".."))
            continue;
        if (index++ < (int)c->arg)
            continue;
        int n = put_entry(s_buf + len, s_buf_size - len, e.d_name, &e.d_stat);
        if (!n) {
            status = XLTS_MORE;
            break;
        }
        len += n;
        count++;
    }
    sceIoDclose(d);
    return reply(c, status, len, count);
}

static int cmd_stat(const XltsHeader *c)
{
    if (c->len > s_buf_size - 1 || recv_payload(c) < 0)
        return c->len > (unsigned)s_buf_size - 1 ? reply(c, XLTS_E_SIZE, 0, 0) : -1;
    int st = take_path(c, P_READ);
    if (st)
        return reply(c, st, 0, 0);
    SceIoStat s;
    memset(&s, 0, sizeof(s));
    int r = sceIoGetstat(s_path, &s);
    if (r < 0)
        return reply(c, io_status(r), 0, r);
    const char *name = strrchr(s_path, '/') + 1;
    int n = put_entry(s_buf, s_buf_size, name, &s);
    return reply(c, 0, n, 1);
}

static int cmd_mkdir(const XltsHeader *c)
{
    if (c->len > s_buf_size - 1 || recv_payload(c) < 0)
        return c->len > (unsigned)s_buf_size - 1 ? reply(c, XLTS_E_SIZE, 0, 0) : -1;
    int st = take_path(c, P_MKDIR);
    if (!st && path_save_depth(s_path) == 2)
        st = XLTS_E_PATH; /* a save folder holds files only */
    if (st)
        return reply(c, st, 0, 0);
    make_dirs(s_path, 1);
    SceIoStat s;
    int r = sceIoGetstat(s_path, &s);
    if (r < 0 || !FIO_S_ISDIR(s.st_mode))
        return reply(c, r < 0 ? io_status(r) : XLTS_E_EXIST, 0, r);
    LOG("mkdir %s", s_path);
    s_changes++;
    return reply(c, 0, 0, 0);
}

static int cmd_delete(const XltsHeader *c)
{
    if (c->len > s_buf_size - 1 || recv_payload(c) < 0)
        return c->len > (unsigned)s_buf_size - 1 ? reply(c, XLTS_E_SIZE, 0, 0) : -1;
    int st = take_path(c, P_CHANGE);
    if (st)
        return reply(c, st, 0, 0);
    SceIoStat s;
    int r = sceIoGetstat(s_path, &s);
    if (r >= 0)
        r = FIO_S_ISDIR(s.st_mode) ? sceIoRmdir(s_path) : sceIoRemove(s_path);
    LOG("delete %s: %08x", s_path, (unsigned)r);
    if (r >= 0) {
        s_changes++;
        if (!FIO_S_ISDIR(s.st_mode))
            space_moved(s_path, -(long long)s.st_size);
    }
    return reply(c, r < 0 ? io_status(r) : 0, 0, r < 0 ? r : 0);
}

static int cmd_rename(const XltsHeader *c)
{
    if (c->len > s_buf_size - 1 || recv_payload(c) < 0)
        return c->len > (unsigned)s_buf_size - 1 ? reply(c, XLTS_E_SIZE, 0, 0) : -1;
    /* "from\0to\0" */
    const char *from = (const char *)s_buf;
    int n1 = strlen(from);
    if (n1 + 2 >= (int)c->len || n1 >= 256)
        return reply(c, XLTS_E_PATH, 0, 0);
    const char *to = from + n1 + 1;
    int n2 = strlen(to);
    if (n1 + 1 + n2 + 1 != (int)c->len || n2 >= 256)
        return reply(c, XLTS_E_PATH, 0, 0);
    snprintf(s_path, sizeof(s_path), "%s", from);
    snprintf(s_path2, sizeof(s_path2), "%s", to);
    if (!path_ok(s_path, P_CHANGE) || !path_ok(s_path2, P_CHANGE) || strncmp(s_path, s_path2, 4))
        return reply(c, XLTS_E_PATH, 0, 0);
    /* in PSP/SAVEDATA: save folders stay save folders, files stay inside one */
    int d1 = path_save_depth(s_path), d2 = path_save_depth(s_path2);
    if ((d1 || d2) && d1 != d2)
        return reply(c, XLTS_E_PATH, 0, 0);
    int r = sceIoRename(s_path, s_path2);
    LOG("rename %s -> %s: %08x", s_path, s_path2, (unsigned)r);
    if (r >= 0)
        s_changes++;
    return reply(c, r < 0 ? io_status(r) : 0, 0, r < 0 ? r : 0);
}

/* DELETE_TREE: a folder and everything in it, done here, so removing a game's folder
 * is one command and not one per file. Walks without recursion (the thread's stack is
 * small): s_path2 is the folder being emptied; a folder found in it is gone into, an
 * emptied one removed, and the walk goes back up until the folder asked for is gone.
 * Each folder is read again after removing files from it (removing while reading may
 * pass entries over). XLT's own folder, and anything holding it, is refused. */
static char s_tree_file[256 + 2];

static int cmd_delete_tree(const XltsHeader *c)
{
    if (c->len > s_buf_size - 1 || recv_payload(c) < 0)
        return c->len > (unsigned)s_buf_size - 1 ? reply(c, XLTS_E_SIZE, 0, 0) : -1;
    int st = take_path(c, P_CHANGE);
    if (st)
        return reply(c, st, 0, 0);
    if (s_log[0]) {
        char app[128];
        snprintf(app, sizeof(app), "%s", s_log);
        char *slash = strrchr(app, '/');
        if (slash)
            *slash = 0;
        if (path_within(app, s_path))
            return reply(c, XLTS_E_PATH, 0, 0);
    }
    XltsRemoved done = {0, 0, 0};
    SceIoStat s;
    int err = sceIoGetstat(s_path, &s);
    if (err >= 0 && !FIO_S_ISDIR(s.st_mode)) {
        err = sceIoRemove(s_path);
        if (err >= 0) {
            done.files = 1;
            done.bytes = s.st_size;
            space_moved(s_path, -(long long)s.st_size);
        }
    } else if (err >= 0) {
        static SceIoDirent e;
        char *w = s_path2;
        snprintf(w, sizeof(s_path2), "%s", s_path);
        int base = strlen(w);
        err = 0;
        for (;;) {
            s_last_cmd = sceKernelGetSystemTimeLow(); /* still talking: the PC is waiting */
            SceUID d = sceIoDopen(w);
            if (d < 0) {
                err = d;
                break;
            }
            int down = 0, removed = 0, wl = strlen(w);
            for (;;) {
                memset(&e, 0, sizeof(e));
                if (sceIoDread(d, &e) <= 0)
                    break;
                if (!strcmp(e.d_name, ".") || !strcmp(e.d_name, ".."))
                    continue;
                if (wl + 1 + (int)strlen(e.d_name) >= (int)sizeof(s_path2)) {
                    err = 0x8001005B; /* ENAMETOOLONG */
                    break;
                }
                if (FIO_S_ISDIR(e.d_stat.st_mode)) {
                    snprintf(w + wl, sizeof(s_path2) - wl, "/%s", e.d_name);
                    down = 1;
                    break;
                }
                snprintf(s_tree_file, sizeof(s_tree_file), "%s/%s", w, e.d_name);
                int r = sceIoRemove(s_tree_file);
                if (r < 0) {
                    LOG("delete tree: %s: %08x", s_tree_file, (unsigned)r);
                    err = r;
                    break;
                }
                done.files++;
                done.bytes += e.d_stat.st_size;
                space_moved(w, -(long long)e.d_stat.st_size);
                removed = 1;
            }
            sceIoDclose(d);
            if (err)
                break;
            if (down || removed)
                continue;
            int r = sceIoRmdir(w);
            if (r < 0) {
                LOG("delete tree: rmdir %s: %08x", w, (unsigned)r);
                err = r;
                break;
            }
            done.folders++;
            if ((int)strlen(w) <= base)
                break; /* the folder asked for is gone */
            *strrchr(w, '/') = 0;
        }
    }
    LOG("delete tree %s: %u files, %u folders, %u KB: %08x", s_path, done.files, done.folders,
        (unsigned)(done.bytes >> 10), (unsigned)err); /* (the kernel's snprintf has no %llu) */
    if (done.files || done.folders)
        s_changes++;
    memcpy(s_buf, &done, sizeof(done));
    return reply(c, err < 0 ? io_status(err) : 0, sizeof(done), err < 0 ? err : 0);
}

/* POWER: the battery. The power service's functions are found by their (stable) NIDs
 * in its user library, as xltshot finds the clock's. */
#define NID_BATT_EXIST   0x0AFD0D8B /* scePowerIsBatteryExist */
#define NID_BATT_PERCENT 0x2085D15D /* scePowerGetBatteryLifePercent */
#define NID_BATT_CHARGE  0x1E490401 /* scePowerIsBatteryCharging */
#define NID_POWER_ONLINE 0x87440F5E /* scePowerIsPowerOnline */
#define NID_BATT_TIME    0x8EFB3FA2 /* scePowerGetBatteryLifeTime */

static int power_call(u32 nid)
{
    int (*f)(void) = (void *)sctrlHENFindFunction("scePower_Service", "scePower", nid);
    return f ? f() : -1;
}

static int cmd_power(const XltsHeader *c)
{
    if (recv_payload(c) < 0)
        return -1;
    XltsPower *p = (XltsPower *)s_buf;
    int exist = power_call(NID_BATT_EXIST);
    p->battery = exist > 0 ? power_call(NID_BATT_PERCENT) : -1;
    p->charging = exist > 0 && power_call(NID_BATT_CHARGE) > 0;
    p->plugged = power_call(NID_POWER_ONLINE) > 0;
    p->minutes = exist > 0 ? power_call(NID_BATT_TIME) : -1;
    if (p->battery < 0 || p->battery > 100)
        p->battery = -1;
    if (p->minutes < 0)
        p->minutes = -1;
    return reply(c, 0, sizeof(*p), 0);
}

/* READ: a piece of a file (where anything may be read). */
static int cmd_read(const XltsHeader *c)
{
    if (c->len <= sizeof(XltsRead) || c->len > sizeof(XltsRead) + 256 || recv_payload(c) < 0)
        return c->len <= sizeof(XltsRead) || c->len > sizeof(XltsRead) + 256 ? reply(c, XLTS_E_SIZE, 0, 0) : -1;
    XltsRead req;
    memcpy(&req, s_buf, sizeof(req));
    const char *path = (const char *)s_buf + sizeof(req);
    int plen = c->len - sizeof(req);
    if (path[plen - 1] != 0 || strlen(path) + 1 != (unsigned)plen)
        return reply(c, XLTS_E_PATH, 0, 0);
    snprintf(s_path, sizeof(s_path), "%s", path);
    if (!path_ok(s_path, P_READ))
        return reply(c, XLTS_E_PATH, 0, 0);
    if (req.length > (unsigned)s_buf_size)
        return reply(c, XLTS_E_SIZE, 0, s_buf_size);
    SceUID fd = sceIoOpen(s_path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return reply(c, io_status(fd), 0, fd);
    int got = 0;
    if (sceIoLseek(fd, (SceOff)req.offset, PSP_SEEK_SET) == (SceOff)req.offset)
        got = req.length ? sceIoRead(fd, s_buf, req.length) : 0;
    else
        got = -1;
    sceIoClose(fd);
    if (got < 0)
        return reply(c, XLTS_E_IO, 0, got);
    return reply(c, 0, got, got);
}

/* GET: a file to the PC (logs, screenshots, recordings). Readable anywhere on ms0:/ef0:. */
static int cmd_get(const XltsHeader *c)
{
    if (c->len > s_buf_size - 1 || recv_payload(c) < 0)
        return c->len > (unsigned)s_buf_size - 1 ? reply(c, XLTS_E_SIZE, 0, 0) : -1;
    unsigned chunk = c->arg ? c->arg : (unsigned)s_buf_size / 2;
    if (chunk > (unsigned)s_buf_size || chunk % 512)
        return reply(c, XLTS_E_SIZE, 0, s_buf_size);
    int st = take_path(c, P_READ);
    if (st)
        return reply(c, st, 0, 0);
    SceIoStat s;
    int r = sceIoGetstat(s_path, &s);
    if (r < 0 || FIO_S_ISDIR(s.st_mode))
        return reply(c, r < 0 ? io_status(r) : XLTS_E_NOENT, 0, r);
    SceUID fd = sceIoOpen(s_path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return reply(c, io_status(fd), 0, fd);
    unsigned size = (unsigned)s.st_size, sent = 0, crc = 0;
    if (reply(c, 0, 0, size) < 0) {
        sceIoClose(fd);
        return -1;
    }
    /* The size is promised: a read that comes up short (the file shrank) is padded with
     * zeros and the final CRC tells the PC. */
    while (sent < size) {
        unsigned n = size - sent < chunk ? size - sent : chunk;
        int got = sceIoRead(fd, s_buf, n);
        if (got < (int)n)
            memset(s_buf + (got > 0 ? got : 0), 0, n - (got > 0 ? got : 0));
        crc = crc_update(crc, s_buf, got > 0 ? got : 0);
        if (send_from(s_buf, n) < 0) {
            sceIoClose(fd);
            return -1;
        }
        sent += n;
        if (got < (int)n) { /* the rest is padding: its CRC won't match, which is the point */
            crc = ~crc;
            while (sent < size) {
                unsigned k = size - sent < chunk ? size - sent : chunk;
                memset(s_buf, 0, k);
                if (send_from(s_buf, k) < 0) {
                    sceIoClose(fd);
                    return -1;
                }
                sent += k;
            }
        }
    }
    sceIoClose(fd);
    LOG("get %s: %u bytes", s_path, size);
    return reply(c, 0, 0, crc);
}

/* The partial file being written, if any. The module stays loaded while XLT stops and
 * starts the driver (around sleep, for one): a thread ended by force in the middle of
 * a file leaves it here, and the next start closes and deletes it. */
static SceUID s_cur_fd = -1;
static char s_cur_part[256 + 8];

static void part_done(void)
{
    s_cur_fd = -1;
    s_cur_part[0] = 0;
}

/* Deletes the partial file; forgotten only once it is really gone. Right after sleep
 * the Memory Stick refuses for a moment: then the next start deletes it. */
static void part_remove(void)
{
    s_cur_fd = -1;
    int r = s_cur_part[0] ? sceIoRemove(s_cur_part) : 0;
    if (r >= 0 || r == (int)0x80010002) /* removed, or not there */
        s_cur_part[0] = 0;
}

/* A PUT whose data has arrived, waiting for the PC's PUT_END. */
static struct {
    int pending;
    char part[256 + 8], final[256];
    unsigned flags, written;
    XltsTime mtime;
} s_put;

static void put_drop(const char *why)
{
    if (!s_put.pending)
        return;
    part_remove();
    LOG("put %s: dropped (%s)", s_put.final, why);
    s_put.pending = 0;
}

static int cmd_put(const XltsHeader *c)
{
    put_drop("a new PUT");
    if (c->len <= sizeof(XltsPut) || c->len > sizeof(XltsPut) + 256 || recv_payload(c) < 0)
        return c->len <= sizeof(XltsPut) || c->len > sizeof(XltsPut) + 256 ? reply(c, XLTS_E_SIZE, 0, 0) : -1;
    XltsPut req;
    memcpy(&req, s_buf, sizeof(req));
    const char *path = (const char *)s_buf + sizeof(req);
    int plen = c->len - sizeof(req);
    if (path[plen - 1] != 0 || strlen(path) + 1 != (unsigned)plen)
        return reply(c, XLTS_E_PATH, 0, 0);
    snprintf(s_path, sizeof(s_path), "%s", path);
    /* in PSP/SAVEDATA only files inside a save folder, not beside them */
    if (!path_ok(s_path, P_CHANGE) || path_save_depth(s_path) == 1)
        return reply(c, XLTS_E_PATH, 0, 0);

    SceIoStat s;
    if (sceIoGetstat(s_path, &s) >= 0 && (FIO_S_ISDIR(s.st_mode) || !(req.flags & XLTS_PUT_REPLACE)))
        return reply(c, XLTS_E_EXIST, 0, 0);
    make_dirs(s_path, 0);
    snprintf(s_put.part, sizeof(s_put.part), "%s" XLTS_PART, s_path);
    snprintf(s_put.final, sizeof(s_put.final), "%s", s_path);
    s_put.flags = req.flags;
    s_put.mtime = req.mtime;
    SceUID fd = sceIoOpen(s_put.part, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) {
        LOG("put %s: open %08x", s_put.part, (unsigned)fd);
        return reply(c, io_status(fd), 0, fd);
    }
    s_cur_fd = fd;
    snprintf(s_cur_part, sizeof(s_cur_part), "%s", s_put.part);
    unsigned half = s_buf_size / 2;
    snprintf(s_name, sizeof(s_name), "%s", strrchr(s_put.final, '/') + 1);
    s_done = 0;
    s_total = req.size;
    s_busy = 1;
    if (reply(c, 0, 0, half) < 0) {
        s_busy = 0;
        sceIoClose(fd);
        part_remove();
        return -1;
    }

    /* The data: one half fills from USB while the other is written to the stick.
     * A write error (stick full) stops the writing, not the receiving: the PC
     * sends it all regardless, and hears about it in the reply. */
    unsigned t0 = sceKernelGetSystemTimeLow(), got = 0, written = 0, crc = 0;
    int err = 0, cur = 0;
    if (req.size) {
        unsigned first = req.size < half ? req.size : half;
        if (recv_queue(s_buf, round_packet(first)) < 0)
            goto lost;
    }
    while (got < req.size) {
        int n = recv_wait(T_DATA);
        if (n <= 0)
            goto lost;
        unsigned char *data = s_buf + cur * half;
        got += n;
        s_done = got;
        if (got < req.size) {
            unsigned next = req.size - got < half ? req.size - got : half;
            if (recv_queue(s_buf + (cur ^ 1) * half, round_packet(next)) < 0)
                goto lost;
        }
        if (!err) {
            /* the write goes on in the background while the CPU takes the checksum
             * (one after the other, the two made sync 30% slower than USB mode) */
            SceInt64 w = sceIoWriteAsync(fd, data, n);
            unsigned c2 = crc_update(crc, data, n);
            if (w >= 0 && sceIoWaitAsync(fd, &w) < 0)
                w = -1;
            if (w != n) {
                err = w < 0 ? (int)w : (int)0x8001001C; /* a short write: the stick is full */
                LOG("put %s: write %08x at %u", s_put.part, (unsigned)w, written);
            } else {
                written += n;
                crc = c2;
            }
        }
        cur ^= 1;
    }
    sceIoClose(fd);
    s_cur_fd = -1; /* closed; the file stays named in s_cur_part until kept or dropped */
    s_busy = 0;
    unsigned us = sceKernelGetSystemTimeLow() - t0;
    if (err || got != req.size) {
        part_remove();
        return reply(c, err ? io_status(err) : XLTS_E_SIZE, 0, err);
    }
    LOG("put %s: %u bytes in %u us (%u KB/s)", s_put.final, written, us,
        us ? (unsigned)((unsigned long long)written * 1000000 / us / 1024) : 0);
    s_put.pending = 1;
    s_put.written = written;
    XltsPutDone *d = (XltsPutDone *)s_buf;
    d->crc32 = crc;
    d->microseconds = us;
    d->written = written;
    return reply(c, 0, sizeof(*d), 0);

lost:
    s_busy = 0;
    sceIoClose(fd);
    part_remove();
    LOG("put %s: link lost after %u of %u bytes", s_put.final, got, req.size);
    return -1;
}

static int cmd_put_end(const XltsHeader *c)
{
    if (!s_put.pending)
        return reply(c, XLTS_E_STATE, 0, 0);
    if (!c->arg) {
        put_drop("the PC said so");
        return reply(c, 0, 0, 0);
    }
    if (s_put.mtime.year) {
        SceIoStat s;
        memset(&s, 0, sizeof(s));
        ScePspDateTime t = {s_put.mtime.year, s_put.mtime.month, s_put.mtime.day,
                            s_put.mtime.hour, s_put.mtime.minute, s_put.mtime.second, 0};
        s.sce_st_mtime = t;
        s.sce_st_ctime = t;
        sceIoChstat(s_put.part, &s, 0x08 | 0x20); /* creation and modification dates */
    }
    int r = 0;
    SceIoStat old;
    if (sceIoGetstat(s_put.final, &old) >= 0) {
        if (!(s_put.flags & XLTS_PUT_REPLACE) || FIO_S_ISDIR(old.st_mode))
            r = 0x80010011;
        else if ((r = sceIoRemove(s_put.final)) >= 0)
            space_moved(s_put.final, -(long long)old.st_size);
    }
    if (r >= 0)
        r = sceIoRename(s_put.part, s_put.final);
    LOG("put %s: %s %08x", s_put.final, r < 0 ? "NOT kept" : "kept", (unsigned)r);
    if (r >= 0) {
        s_changes++;
        space_moved(s_put.final, s_put.written);
    }
    if (r < 0)
        part_remove();
    else
        part_done();
    s_put.pending = 0;
    return reply(c, r < 0 ? io_status(r) : 0, 0, r < 0 ? r : 0);
}

/* --------------------------------------------------------------- the thread */

static void log_controls(int *logged)
{
    for (; *logged < s_ctl_count; (*logged)++) {
        struct CtlEntry *e = &s_ctl[*logged];
        LOG("control: type %02x req %02x value %04x index %04x length %u -> %d",
            e->type, e->req, e->value, e->index, e->length, e->answered);
    }
}

static int s_logged;

static int session(void)
{
    static XltsHeader c __attribute__((aligned(64)));
    static unsigned char hbuf[512] __attribute__((aligned(64)));
    LOG("attached, %s speed", s_speed == 2 ? "high" : "full");
    for (;;) {
        log_controls(&s_logged);
        int n = recv_into(hbuf, sizeof(hbuf), T_IDLE);
        if (n < 0)
            return -1;
        if (n != sizeof(c)) {
            LOG("command of %d bytes ignored", n);
            continue;
        }
        memcpy(&c, hbuf, sizeof(c));
        if (c.magic != XLTS_MAGIC) {
            LOG("bad magic %08x", c.magic);
            continue;
        }
        if (c.op != XLTS_PUT_END)
            put_drop("another command came first");
        s_last_cmd = sceKernelGetSystemTimeLow();
        int r;
        switch (c.op) {
        case XLTS_HELLO:   r = cmd_hello(&c); break;
        case XLTS_ECHO:    r = cmd_echo(&c); break;
        case XLTS_SINK:    r = cmd_sink(&c); break;
        case XLTS_SOURCE:  r = cmd_source(&c); break;
        case XLTS_CTLLOG:  r = cmd_ctllog(&c); break;
        case XLTS_DF:      r = cmd_df(&c); break;
        case XLTS_LIST:    r = cmd_list(&c); break;
        case XLTS_STAT:    r = cmd_stat(&c); break;
        case XLTS_MKDIR:   r = cmd_mkdir(&c); break;
        case XLTS_PUT:     r = cmd_put(&c); break;
        case XLTS_PUT_END: r = cmd_put_end(&c); break;
        case XLTS_DELETE:  r = cmd_delete(&c); break;
        case XLTS_RENAME:  r = cmd_rename(&c); break;
        case XLTS_GET:     r = cmd_get(&c); break;
        case XLTS_DELETE_TREE: r = cmd_delete_tree(&c); break;
        case XLTS_READ:    r = cmd_read(&c); break;
        case XLTS_POWER:   r = cmd_power(&c); break;
        default:           r = reply(&c, XLTS_E_OP, 0, 0); break;
        }
        if (r < 0) {
            LOG("command %u (seq %u) failed", c.op, c.seq);
            return -1;
        }
    }
}

static int sync_thread(SceSize args, void *argp)
{
    for (;;) {
        if (!s_speed) { /* not attached: wait for the cable */
            u32 got = 0;
            sceKernelWaitEventFlag(s_ev, EV_ATTACH | EV_QUIT, PSP_EVENT_WAITOR, &got, NULL);
            if (got & EV_QUIT)
                break;
        }
        sceKernelClearEventFlag(s_ev, ~(EV_ATTACH | EV_DETACH));
        if (!s_speed)
            continue; /* it went again already */
        session();
        s_talking = 0;
        s_busy = 0;
        put_drop("the session ended");
        sceUsbbdReqCancelAll(&s_ep[1]);
        sceUsbbdReqCancelAll(&s_ep[2]);
        log_controls(&s_logged);
        u32 now = 0;
        sceKernelPollEventFlag(s_ev, EV_QUIT, PSP_EVENT_WAITOR, &now);
        if (now & EV_QUIT)
            break;
        /* still attached (the PC program ended or sent nonsense): a new session on the same cable */
        LOG("session over (%s)", s_speed ? "still attached" : "detached");
        if (s_speed)
            sceKernelDelayThread(100000);
    }
    LOG("thread done");
    return 0;
}

/* --------------------------------------------------------- driver start/stop */

static void fill(struct UsbData *d, struct DeviceDescriptor *dev, struct ConfigDescriptor *conf,
                 struct InterfaceDescriptor *inter, struct EndpointDescriptor *endp)
{
    memset(d, 0, sizeof(*d));
    memcpy(d->devdesc, dev, sizeof(*dev));
    d->config.pconfdesc = &d->confdesc;
    d->config.pinterfaces = &d->interfaces;
    d->config.pinterdesc = &d->interdesc;
    d->config.pendp = d->endp;
    memcpy(d->confdesc.desc, conf, sizeof(*conf));
    d->confdesc.pinterfaces = &d->interfaces;
    d->interfaces.pinterdesc[0] = &d->interdesc;
    d->interfaces.intcount = 1;
    memcpy(d->interdesc.desc, inter, sizeof(*inter));
    d->interdesc.pendp = d->endp;
    memcpy(d->endp[0].desc, &endp[0], sizeof(endp[0]));
    memcpy(d->endp[1].desc, &endp[1], sizeof(endp[1]));
}

extern struct UsbDriver g_driver;

static int drv_start(int size, void *p)
{
    if (s_cur_fd >= 0) /* a thread ended by force in the middle of a file (sleep, for one) */
        sceIoClose(s_cur_fd);
    s_cur_fd = -1;
    if (s_cur_part[0]) {
        int r = sceIoRemove(s_cur_part);
        LOG("left by the last run: %s removed (%08x)", s_cur_part, (unsigned)r);
        if (r >= 0 || r == (int)0x80010002)
            s_cur_part[0] = 0; /* else the next start tries again */
    }
    s_put.pending = 0;
    memset(s_space, 0, sizeof(s_space)); /* after sleep it may be another Memory Stick */
    s_dev_full = s_dev_hi;
    s_conf_full = s_conf_hi;
    s_inter_full = s_inter_hi;
    s_endp_full[0] = s_endp_hi[0];
    s_endp_full[1] = s_endp_hi[1];
    s_endp_full[0].wMaxPacketSize = s_endp_full[1].wMaxPacketSize = 64;
    fill(&s_data[0], &s_dev_hi, &s_conf_hi, &s_inter_hi, s_endp_hi);
    fill(&s_data[1], &s_dev_full, &s_conf_full, &s_inter_full, s_endp_full);
    g_driver.devp_hi = s_data[0].devdesc;
    g_driver.confp_hi = &s_data[0].config;
    g_driver.devp = s_data[1].devdesc;
    g_driver.confp = &s_data[1].config;

    s_speed = 0;
    s_ctl_count = 0;
    s_logged = 0;
    s_ev = sceKernelCreateEventFlag("XLTSyncEv", 0, 0, NULL);
    if (s_ev < 0) {
        LOG("event flag: %08x", (unsigned)s_ev);
        return -1;
    }
    s_thread = sceKernelCreateThread("XLTSync", sync_thread, 0x18, 0x4000, 0, NULL);
    if (s_thread < 0 || sceKernelStartThread(s_thread, 0, NULL) < 0) {
        LOG("thread: %08x", (unsigned)s_thread);
        return -1;
    }
    LOG("driver started, buffer %d bytes", s_buf_size);
    return 0;
}

static int drv_stop(int size, void *p)
{
    if (s_thread >= 0) {
        sceKernelSetEventFlag(s_ev, EV_QUIT);
        SceUInt t = 3000000; /* the thread may be waiting for the Memory Stick (after sleep) */
        if (sceKernelWaitThreadEnd(s_thread, &t) < 0)
            sceKernelTerminateThread(s_thread);
        sceKernelDeleteThread(s_thread);
        s_thread = -1;
    }
    if (s_ev >= 0) {
        sceKernelDeleteEventFlag(s_ev);
        s_ev = -1;
    }
    LOG("driver stopped");
    return 0;
}

struct UsbDriver g_driver = {
    XLTSYNC_DRIVER, 3, s_ep, &s_intp,
    NULL, NULL, NULL, NULL,
    (struct StringDescriptor *)s_str,
    cb_control, cb_func28, cb_attach, cb_detach,
    0, drv_start, drv_stop, NULL,
};

/* ------------------------------------------------------------ module entry */

/* argp: the log file's path (XLT's folder), NUL-terminated */
int module_start(SceSize args, void *argp)
{
    if (args > 0 && argp) {
        int n = args < sizeof(s_log) - 1 ? (int)args : (int)sizeof(s_log) - 1;
        memcpy(s_log, argp, n);
        s_log[n] = 0;
        SceUID fd = sceIoOpen(s_log, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
        if (fd >= 0) sceIoClose(fd);
    }
    crc_init();
    build_ms_props();
    build_msos20();
    /* the big buffer: 256 KB of kernel memory, else what the static one gives */
    s_buf_block = sceKernelAllocPartitionMemory(1, "XLTSyncBuf", PSP_SMEM_High, 256 * 1024 + 64, NULL);
    if (s_buf_block >= 0) {
        s_buf = (unsigned char *)(((unsigned)sceKernelGetBlockHeadAddr(s_buf_block) + 63) & ~63u);
        s_buf_size = 256 * 1024;
    } else {
        LOG("no 256 KB buffer (%08x): %d bytes", (unsigned)s_buf_block, s_buf_size);
    }
    int r = sceUsbbdRegister(&g_driver);
    LOG("XLTSync protocol %d: sceUsbbdRegister %08x", XLTS_PROTO, (unsigned)r);
    return r < 0 ? 1 : 0; /* 1: the module stops again */
}

int module_stop(SceSize args, void *argp)
{
    int r = sceUsbbdUnregister(&g_driver);
    LOG("sceUsbbdUnregister %08x", (unsigned)r);
    if (s_buf_block >= 0) {
        sceKernelFreePartitionMemory(s_buf_block);
        s_buf_block = -1;
    }
    return 0;
}
