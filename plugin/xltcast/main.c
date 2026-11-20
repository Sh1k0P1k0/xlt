/* xltcast - the game's picture on the PC over USB (XLT Cast).
 *
 * A kernel plugin ARK loads at the "game" runlevel. A few seconds into the game it
 * loads the system's USB driver, registers its own USB device (XLT sync's IDs,
 * interface protocol 'C') and waits for tools/xltcast.py. The PC starts and stops
 * the frames and reads counters; protocol in xltcast.h.
 *
 * Two threads: a watcher that wakes at every vertical blank and counts frame
 * buffer changes (the game's own frame rate, casting or not), and the sender.
 *
 * The USB functions are looked up once usb.prx is loaded (it isn't, in most
 * games), so the plugin loads in any game. The bus driver structures and the
 * transfers are XLT sync's (plugin/xltsync/main.c), which follow PSPLINK's
 * usbhostfs. Everything is logged to SEPLUGINS/xltcast.log. */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspsysmem_kernel.h>
#include <pspmodulemgr_kernel.h>
#include <psploadcore.h>
#include <pspinit.h>
#include <pspusb.h>
#include <pspusbbus.h>
#include <stddef.h>
#include <string.h>
#include <systemctrl.h>
/* from the kernel's SysclibForKernel (libpspsysclib_kernel_660), which the SDK doesn't declare */
int snprintf(char *buf, size_t n, const char *fmt, ...);
#include "xltcast.h"
#include "audio.h"

PSP_MODULE_INFO("XLTCast", PSP_MODULE_KERNEL, 0, 1);

#define START_DELAY_US 4000000 /* let the game load its own modules first */

/* ------------------------------------------------------------------ log */

static char s_dev[8] = "ms0:";

void log_line(const char *line, int truncate) /* (audio.c logs too) */
{
    char p[64];
    snprintf(p, sizeof(p), "%sSEPLUGINS/xltcast.log", s_dev);
    SceUID fd = sceIoOpen(p, PSP_O_WRONLY | PSP_O_CREAT | (truncate ? PSP_O_TRUNC : PSP_O_APPEND), 0777);
    if (fd < 0) return;
    sceIoWrite(fd, line, strlen(line));
    sceIoWrite(fd, "\n", 1);
    sceIoClose(fd);
}
#define LOG(...) do { char l_[200]; int n_ = snprintf(l_, sizeof(l_), "%u ", sceKernelGetSystemTimeLow() / 1000); \
                      snprintf(l_ + n_, sizeof(l_) - n_, __VA_ARGS__); log_line(l_, 0); } while (0)

/* ------------------------------------------------------- looked-up functions */

#define NID_DISP_GETBUF   0xEEDA2E54
#define NID_DISP_VBLANK   0x984C27E7
#define NID_GE_DRAWSYNC   0xB287BD61
#define NID_USB_START     0xAE5DE6AF
#define NID_USB_STOP      0xC2464FA0
#define NID_USB_ACTIVATE  0x586DB82C
#define NID_USB_DEACT     0xC572A9C8
#define NID_BD_REGISTER   0xB1644BE7
#define NID_BD_UNREGISTER 0xC1E2A540
#define NID_BD_SEND       0x23E51D8F
#define NID_BD_RECV       0x913EC15D
#define NID_BD_CANCELALL  0xC5E53685

static int (*f_getbuf)(void **, int *, int *, int);
static int (*f_vblank)(void);
static int (*f_ge_drawsync)(int); /* sceGeDrawSync: 1 = only asks (0: the GE is idle); never waits or queues */
static int (*f_usb_start)(const char *, int, void *);
static int (*f_usb_stop)(const char *, int, void *);
static int (*f_usb_activate)(u32);
static int (*f_usb_deactivate)(u32);
static int (*f_bd_register)(struct UsbDriver *);
static int (*f_bd_unregister)(struct UsbDriver *);
static int (*f_bd_send)(struct UsbdDeviceReq *);
static int (*f_bd_recv)(struct UsbdDeviceReq *);
static int (*f_bd_cancel_all)(struct UsbEndpoint *);

static void *find(const char *mod, const char *lib, u32 nid)
{
    return (void *)sctrlHENFindFunction(mod, lib, nid);
}

/* usb.prx's module name (not checked on a PSP yet: the log says which one worked) */
static const char *s_usb_mod;

static void *find_usb(const char *lib, u32 nid)
{
    static const char *names[] = {"sceUSB_Driver", "sceUsb_Driver", "sceUSB_Bus_Driver"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        void *f = find(names[i], lib, nid);
        if (f) {
            s_usb_mod = names[i];
            return f;
        }
    }
    return NULL;
}

/* ---------------------------------------------------------------- cache */

/* as XLT sync: the USB controller reads RAM itself (hit writeback invalidate, 64-byte lines) */
static void dcache_flush(const void *p, int n)
{
    unsigned a = (unsigned)p & ~63u, end = ((unsigned)p + n + 63) & ~63u;
    for (; a < end; a += 64)
        __asm__ volatile("cache 0x1B, 0(%0)" : : "r"(a) : "memory");
    __asm__ volatile("sync" : : : "memory");
}

/* ---------------------------------------------------------- descriptors */

#define EP_IN 0x81
#define EP_OUT 0x02

static struct DeviceDescriptor s_dev_hi = {
    .bLength = 18, .bDescriptorType = 0x01, .bcdUSB = 0x201, /* 2.01: a BOS descriptor (MS OS 2.0) */
    .bDeviceClass = 0, .bDeviceSubClass = 0, .bDeviceProtocol = 0, .bMaxPacketSize = 64,
    .idVendor = 0, .idProduct = 0, .bcdDevice = XLTCAST_BCD,
    .iManufacturer = 0, .iProduct = 0, .iSerialNumber = 0, .bNumConfigurations = 1,
};
static struct ConfigDescriptor s_conf_hi = {
    .bLength = 9, .bDescriptorType = 2, .wTotalLength = 9 + 9 + 2 * 7, .bNumInterfaces = 1,
    .bConfigurationValue = 1, .iConfiguration = 0, .bmAttributes = 0xC0, .bMaxPower = 0,
};
static struct InterfaceDescriptor s_inter_hi = {
    .bLength = 9, .bDescriptorType = 4, .bInterfaceNumber = 0, .bAlternateSetting = 0,
    .bNumEndpoints = 2, .bInterfaceClass = 0xFF, .bInterfaceSubClass = XLTCAST_SUBCLASS,
    .bInterfaceProtocol = XLTCAST_PROTOCOL, .iInterface = 1,
};
static struct EndpointDescriptor s_endp_hi[2] = {
    {.bLength = 7, .bDescriptorType = 5, .bEndpointAddress = EP_IN, .bmAttributes = 2, .wMaxPacketSize = 512, .bInterval = 0},
    {.bLength = 7, .bDescriptorType = 5, .bEndpointAddress = EP_OUT, .bmAttributes = 2, .wMaxPacketSize = 512, .bInterval = 0},
};
static struct DeviceDescriptor s_dev_full;
static struct ConfigDescriptor s_conf_full;
static struct InterfaceDescriptor s_inter_full;
static struct EndpointDescriptor s_endp_full[2];

static unsigned char s_str[] = {18, 3, 'X', 0, 'L', 0, 'T', 0, ' ', 0, 'C', 0, 'a', 0, 's', 0, 't', 0};

static struct UsbEndpoint s_ep[3] = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
static struct UsbInterface s_intp = {0xFFFFFFFF, 0, 1};
static struct UsbData s_data[2];

/* Windows: the same answers as XLT sync's device (plugin/xltsync/main.c), so Windows gives
 * the Cast device its WinUSB driver by itself, with nothing to install. */
/* Microsoft OS descriptors 1.0 (Windows 7 and later, all of them still):
 * string 0xEE says which vendor request to ask, which then gives the
 * compatible ID (WINUSB) and the interface GUID programs find the device by. */
static const unsigned char s_ms_string[18] = {
    18, 3, 'M', 0, 'S', 0, 'F', 0, 'T', 0, '1', 0, '0', 0, '0', 0, XLTCAST_VENDOR_REQ, 0,
};
static const unsigned char s_ms_compat[40] = {
    40, 0, 0, 0, 0x00, 0x01, 4, 0, 1, 0, 0, 0, 0, 0, 0, 0, /* length, version 1.0, index 4, 1 function */
    0, 1, 'W', 'I', 'N', 'U', 'S', 'B', 0, 0,              /* interface 0, compatible ID */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,              /* sub-compatible ID, reserved */
};
#define XLTCAST_GUID "{6F1C2E3A-9B4D-4E57-8A60-C7D2B5E19F48}" /* not sync's: programs can tell the two apart */
static unsigned char s_ms_props[142]; /* built at start: UTF-16 is long-hand */

static void build_ms_props(void)
{
    static const char name[] = "DeviceInterfaceGUID", guid[] = XLTCAST_GUID;
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
    static const char name[] = "DeviceInterfaceGUIDs", guid[] = XLTCAST_GUID;
    unsigned char *p = s_bos;
#define PUT32(v) do { unsigned v_ = (v); *p++ = v_; *p++ = v_ >> 8; *p++ = v_ >> 16; *p++ = v_ >> 24; } while (0)
#define PUT16(v) do { unsigned v_ = (v); *p++ = v_; *p++ = v_ >> 8; } while (0)
    *p++ = 5; *p++ = 0x0F; PUT16(sizeof(s_bos)); *p++ = 1;           /* BOS: one capability */
    *p++ = 28; *p++ = 0x10; *p++ = 0x05; *p++ = 0;                  /* platform capability */
    memcpy(p, uuid, 16); p += 16;
    PUT32(0x06030000); PUT16(MSOS20_SET_LEN); *p++ = XLTCAST_VENDOR_REQ; *p++ = 0;

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

enum { EV_ATTACH = 1, EV_DETACH = 2, EV_IN_DONE = 4, EV_OUT_DONE = 8, EV_QUIT = 16, EV_FLIP = 32 };
static SceUID s_ev = -1, s_thread = -1, s_watch = -1, s_starter = -1;
static volatile int s_speed, s_run = 1;
static struct UsbdDeviceReq s_in_req, s_out_req;

/* written by the watcher, read by the sender */
static volatile unsigned s_flips, s_vblanks, s_fmt, s_stride;
static void *volatile s_top;

static XltcStats s_st;
static int s_mode, s_every = 1, s_method;
static int s_homebrew, s_skip; /* set at the start: a homebrew program; XLT or ARK's launcher */

/* copy mode: two pieces, one filled while the other is sent */
#define PIECE_MAX (480 * XLTC_PIECE_ROWS * 2)
static unsigned char *s_copy[2];
static SceUID s_copy_block = -1;
/* 1: s_copy[0] holds a whole frame (copied in one go right after the game shows it, then sent);
 * 0: two pieces, each sent while the next is copied (when there's too little memory for a whole one) */
static int s_full;
#define FULL_SIZE (480 * XLTC_H * 2)

/* Copy mode's memory is taken only while casting and given back at once: a game may
 * need all of the kernel's memory later (loading its own modules), and nothing should
 * be missing from it while nobody is watching. */
static int copy_alloc(void)
{
    if (s_copy_block >= 0)
        return 0;
    /* A whole frame: copied piece by piece with sending in between, a game at 60 fps draws the
     * next picture into it before the copy ends: horizontal bands (NFSU2VIEW's menus, 2026-11-20) */
    s_full = 1;
    s_copy_block = sceKernelAllocPartitionMemory(1, "XLTCastBuf", PSP_SMEM_High, FULL_SIZE + 64, NULL);
    if (s_copy_block < 0) {
        s_full = 0;
        LOG("no room for a whole frame (%08x): pieces", (unsigned)s_copy_block);
        s_copy_block = sceKernelAllocPartitionMemory(1, "XLTCastBuf", PSP_SMEM_High, 2 * PIECE_MAX + 64, NULL);
    }
    if (s_copy_block < 0) {
        LOG("copy buffer: %08x", (unsigned)s_copy_block);
        s_st.copy_buf = 0;
        return -1;
    }
    s_copy[0] = (unsigned char *)(((unsigned)sceKernelGetBlockHeadAddr(s_copy_block) + 63) & ~63u);
    s_copy[1] = s_full ? NULL : s_copy[0] + PIECE_MAX;
    s_st.copy_buf = s_full ? FULL_SIZE : 2 * PIECE_MAX;
    return 0;
}

static void copy_free(void)
{
    if (s_copy_block >= 0)
        sceKernelFreePartitionMemory(s_copy_block);
    s_copy_block = -1;
    s_copy[0] = s_copy[1] = NULL;
}

/* ------------------------------------------------------- bus callbacks */

static int cb_in_done(struct UsbdDeviceReq *r, int a2, int a3) { sceKernelSetEventFlag(s_ev, EV_IN_DONE); return 0; }
static int cb_out_done(struct UsbdDeviceReq *r, int a2, int a3) { sceKernelSetEventFlag(s_ev, EV_OUT_DONE); return 0; }
static struct UsbdDeviceReq s_ctl_req;
static unsigned char s_ctl_reply[192] __attribute__((aligned(64)));
static int cb_ctl_done(struct UsbdDeviceReq *r, int a2, int a3) { return 0; }

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
    return f_bd_send(&s_ctl_req);
}

/* Microsoft's string 0xEE, the BOS descriptor and our vendor requests (as XLT sync); other vendor
 * requests are refused, everything else is left to the bus driver. */
static int cb_control(int a1, int a2, struct DeviceRequest *req)
{
    unsigned type = req->bmRequestType, desc = req->wValue >> 8;
    int ours = req->bRequest == XLTCAST_VENDOR_REQ && (type == 0xC0 || type == 0xC1);
    if (type == 0x80 && req->bRequest == 6 && req->wValue == 0x03EE)
        ep0_send(s_ms_string, sizeof(s_ms_string), req->wLength);
    else if (type == 0x80 && req->bRequest == 6 && desc == 0x0F)
        ep0_send(s_bos, sizeof(s_bos), req->wLength);
    else if (ours && req->wIndex == 4)
        ep0_send(s_ms_compat, sizeof(s_ms_compat), req->wLength);
    else if (ours && req->wIndex == 5)
        ep0_send(s_ms_props, sizeof(s_ms_props), req->wLength);
    else if (ours && req->wIndex == 7)
        ep0_send(s_msos20, sizeof(s_msos20), req->wLength);
    else if ((type & 0x60) == 0x40)
        return -1;
    return 0;
}
static int cb_func28(int a1, int a2, int a3) { return 0; }
static int cb_attach(int speed, void *a2, void *a3) { s_speed = speed; sceKernelSetEventFlag(s_ev, EV_ATTACH); return 0; }
static int cb_detach(int a1, int a2, int a3) { s_speed = 0; sceKernelSetEventFlag(s_ev, EV_DETACH); return 0; }

/* ---------------------------------------------------------- transfers */

#define T_DATA 3000000

static int wait_done(unsigned bit, struct UsbdDeviceReq *r, SceUInt timeout)
{
    u32 got = 0;
    SceUInt t = timeout;
    int ret = sceKernelWaitEventFlag(s_ev, bit | EV_DETACH | EV_QUIT, PSP_EVENT_WAITOR, &got, &t);
    if (ret < 0 || !(got & bit)) {
        f_bd_cancel_all(r->endp);
        if (ret < 0) LOG("wait %x: %08x", bit, (unsigned)ret);
        s_st.last_err = ret < 0 ? (unsigned)ret : 0xDE7AC4ED;
        return -1;
    }
    sceKernelClearEventFlag(s_ev, ~bit);
    if (r->retcode != 0) {
        LOG("transfer %x: retcode %d, %d bytes", bit, r->retcode, r->recvsize);
        s_st.last_err = r->retcode;
        return -1;
    }
    return r->recvsize;
}

static unsigned char s_cmd[512] __attribute__((aligned(64)));

static int recv_queue(void)
{
    dcache_flush(s_cmd, sizeof(s_cmd));
    memset(&s_out_req, 0, sizeof(s_out_req));
    s_out_req.endp = &s_ep[2];
    s_out_req.data = s_cmd;
    s_out_req.size = sizeof(s_cmd);
    s_out_req.func = cb_out_done;
    sceKernelClearEventFlag(s_ev, ~EV_OUT_DONE);
    int r = f_bd_recv(&s_out_req);
    if (r < 0) LOG("ReqRecv: %08x", (unsigned)r);
    return r < 0 ? -1 : 0;
}

/* starts one IN transfer; p must be flushed already (or uncached) */
static int send_start(const void *p, int n)
{
    memset(&s_in_req, 0, sizeof(s_in_req));
    s_in_req.endp = &s_ep[1];
    s_in_req.data = (void *)p;
    s_in_req.size = n;
    s_in_req.func = cb_in_done;
    sceKernelClearEventFlag(s_ev, ~EV_IN_DONE);
    int r = f_bd_send(&s_in_req);
    if (r < 0) {
        LOG("ReqSend: %08x", (unsigned)r);
        s_st.last_err = r;
        return -1;
    }
    return 0;
}

static int send_end(int n) { return wait_done(EV_IN_DONE, &s_in_req, T_DATA) == n ? 0 : -1; }

static int send_from(const void *p, int n)
{
    dcache_flush(p, n);
    return send_start(p, n) < 0 ? -1 : send_end(n);
}

static int header(unsigned op, unsigned status, unsigned seq, unsigned len, unsigned a, unsigned b, unsigned c, unsigned d)
{
    static XltcHeader h __attribute__((aligned(64)));
    h.magic = XLTC_MAGIC;
    h.op = op;
    h.status = status;
    h.seq = seq;
    h.len = len;
    h.a = a; h.b = b; h.c = c; h.d = d;
    return send_from(&h, sizeof(h));
}

static int reply_stats(const XltcHeader *c)
{
    static XltcStats out __attribute__((aligned(64)));
    s_st.version = XLTC_VERSION;
    s_st.flips = s_flips;
    s_st.vblanks = s_vblanks;
    s_st.fmt = s_fmt;
    s_st.stride = s_stride;
    s_st.mode = s_mode;
    s_st.speed = s_speed;
    s_st.audio_hooks = audio_hooked();
    out = s_st;
    if (header(c->op | XLTC_REPLY, XLTC_OK, c->seq, sizeof(out), 0, 0, 0, 0) < 0)
        return -1;
    return send_from(&out, sizeof(out));
}

/* -------------------------------------------------------------- frames */

#define GE_IDLE_WAIT_US 12000

static inline unsigned short to565(u32 c)
{
    return (c >> 3 & 0x1F) | (c >> 10 & 0x3F) << 5 | (c >> 19 & 0x1F) << 11;
}

/* rows y0..y0+n of the frame into dst, 480 wide, 16-bit */
static void copy_rows(unsigned short *dst, const void *vram, int stride, int fmt, int y0, int n)
{
    if (fmt == PSP_DISPLAY_PIXEL_FORMAT_8888) {
        const u32 *src = (const u32 *)vram + y0 * stride;
        for (int y = 0; y < n; y++, src += stride, dst += 480)
            for (int x = 0; x < 480; x++)
                dst[x] = to565(src[x]);
    } else {
        const unsigned short *src = (const unsigned short *)vram + y0 * stride;
        for (int y = 0; y < n; y++, src += stride, dst += 480)
            memcpy(dst, src, 960);
    }
}

static int send_frame(unsigned number)
{
    void *top = s_top;
    int stride = s_stride, fmt = s_fmt;
    if (!top || stride < 480)
        return 0;
    u32 t0 = sceKernelGetSystemTimeLow();
    int bpp = fmt == PSP_DISPLAY_PIXEL_FORMAT_8888 ? 4 : 2;
    int r = 0;
    if (s_mode == XLTC_DIRECT) {
        /* the controller reads the frame buffer: whole rows of the buffer's width */
        u8 *cached = (u8 *)((u32)top & ~0x40000000u);
        u8 *uncached = (u8 *)((u32)top | 0x40000000u);
        int row = stride * bpp;
        if (header(XLTC_FRAME, 0, 0, row * XLTC_H, fmt, stride, number, s_st.send_us) < 0)
            return -1;
        dcache_flush(cached, row * XLTC_H);
        for (int y = 0; y < XLTC_H && r == 0; y += XLTC_PIECE_ROWS) {
            int n = XLTC_H - y < XLTC_PIECE_ROWS ? XLTC_H - y : XLTC_PIECE_ROWS;
            r = send_start(uncached + y * row, n * row) < 0 ? -1 : send_end(n * row);
        }
    } else {
        int method = s_method == XLTC_M_UNCACHED ? XLTC_M_UNCACHED : XLTC_M_CACHED;
        s_st.method = method;
        const u8 *cached = (const u8 *)((u32)top & ~0x40000000u);
        const u8 *vram = method == XLTC_M_UNCACHED ? (const u8 *)((u32)top | 0x40000000u) : cached;
        int out_fmt = bpp == 4 ? PSP_DISPLAY_PIXEL_FORMAT_565 : fmt;
        int out_row = 960;
        if (s_full) {
            /* Some games show a buffer before the GE has finished drawing it (a racing game's road
             * there, its sky still black: Windows test, 2026-11-20). So the copy waits until the GE
             * says it is idle, GE_IDLE_WAIT_US at most (then it copies anyway, as before). Only a
             * question to the GE: nothing is queued on it (a queued copy job froze the PSP). */
            if (f_ge_drawsync) {
                u32 w0 = sceKernelGetSystemTimeLow();
                int st;
                while ((st = f_ge_drawsync(1)) > 0 && sceKernelGetSystemTimeLow() - w0 < GE_IDLE_WAIT_US)
                    sceKernelDelayThread(250);
                u32 waited = sceKernelGetSystemTimeLow() - w0;
                s_st.ge_wait_us += waited;
                if (waited > 300) s_st.ge_waits++;
                if (st > 0) s_st.ge_timeouts++;
            }
            /* the whole frame first, at once (4.5 ms 16-bit, 13 ms 32-bit, inside a 60 fps game's
             * 16.7), then sent; if the game showed another meanwhile, it was drawing into this one */
            u32 g = sceKernelGetSystemTimeLow();
            unsigned char *buf = s_copy[0];
            for (int y = 0; y < XLTC_H; y += XLTC_PIECE_ROWS) {
                int n = XLTC_H - y < XLTC_PIECE_ROWS ? XLTC_H - y : XLTC_PIECE_ROWS;
                if (method != XLTC_M_UNCACHED)
                    dcache_flush(cached + y * stride * bpp, n * stride * bpp);
                copy_rows((unsigned short *)(buf + y * out_row), vram, stride, fmt, y, n);
            }
            dcache_flush(buf, FULL_SIZE);
            s_st.grab_us += sceKernelGetSystemTimeLow() - g;
            if (s_flips != number) {
                s_st.dma_err++; /* (dropped: the game drew over it while it was copied) */
                return 0;
            }
            if (header(XLTC_FRAME, 0, 0, out_row * XLTC_H, out_fmt, 480, number, s_st.send_us) < 0)
                return -1;
            for (int y = 0; y < XLTC_H && r == 0; y += XLTC_PIECE_ROWS) {
                int n = XLTC_H - y < XLTC_PIECE_ROWS ? XLTC_H - y : XLTC_PIECE_ROWS;
                r = send_start(buf + y * out_row, n * out_row) < 0 ? -1 : send_end(n * out_row);
            }
            goto copied;
        }
        if (header(XLTC_FRAME, 0, 0, out_row * XLTC_H, out_fmt, 480, number, s_st.send_us) < 0)
            return -1;
        int k = 0, pending = 0;
        for (int y = 0; y < XLTC_H && r == 0; y += XLTC_PIECE_ROWS, k ^= 1) {
            int n = XLTC_H - y < XLTC_PIECE_ROWS ? XLTC_H - y : XLTC_PIECE_ROWS;
            int bytes = n * out_row;
            u32 g = sceKernelGetSystemTimeLow();
            /* what the CPU's cache holds of these rows: written back (a game drawing with the
             * CPU) and dropped, so the reads below see the frame as it is now */
            if (method != XLTC_M_UNCACHED)
                dcache_flush(cached + y * stride * bpp, n * stride * bpp);
            copy_rows((unsigned short *)s_copy[k], vram, stride, fmt, y, n);
            dcache_flush(s_copy[k], bytes);
            s_st.grab_us += sceKernelGetSystemTimeLow() - g;
            if (pending && send_end(pending) < 0) { r = -1; break; }
            pending = 0;
            if (send_start(s_copy[k], bytes) < 0) { r = -1; break; }
            pending = bytes;
        }
        if (r == 0 && pending && send_end(pending) < 0)
            r = -1;
    }
copied:;
    u32 took = sceKernelGetSystemTimeLow() - t0;
    s_st.send_us += took;
    if (r < 0) {
        s_st.failed++;
        LOG("frame %u (mode %d, fmt %d, stride %d) failed: %08x", number, s_mode, fmt, stride, s_st.last_err);
        return -1;
    }
    s_st.sent++;
    return 0;
}

/* ------------------------------------------------------------ commands */

static int command(const XltcHeader *c)
{
    switch (c->op) {
    case XLTC_HELLO:
    case XLTC_STATS:
        return reply_stats(c);
    case XLTC_START:
        if (c->a != XLTC_DIRECT && c->a != XLTC_COPY)
            return header(c->op | XLTC_REPLY, XLTC_E_MODE, c->seq, 0, 0, 0, 0, 0);
        if (c->a == XLTC_COPY && copy_alloc() < 0)
            return header(c->op | XLTC_REPLY, XLTC_E_MEM, c->seq, 0, 0, 0, 0, 0);
        if (c->b >= 8 && c->b <= 0x7F) {
            sceKernelChangeThreadPriority(0, c->b);
            s_st.prio = c->b;
        }
        s_every = c->c > 1 ? c->c : 1;
        s_method = (int)c->d;
        s_mode = c->a;
        int snd = audio_start();
        LOG("start: mode %d, method %d, priority 0x%x, every %d; sound %s", s_mode, s_method, s_st.prio, s_every,
            snd == 0 ? "on" : audio_hooked() ? "NO MEMORY" : "not hooked");
        return header(c->op | XLTC_REPLY, XLTC_OK, c->seq, 0, 0, 0, 0, 0);
    case XLTC_STOP:
        s_mode = 0;
        copy_free();
        audio_stop();
        LOG("stop: %u sent, %u failed", s_st.sent, s_st.failed);
        return header(c->op | XLTC_REPLY, XLTC_OK, c->seq, 0, 0, 0, 0, 0);
    }
    return header(c->op | XLTC_REPLY, XLTC_E_OP, c->seq, 0, 0, 0, 0, 0);
}

static int session(void)
{
    LOG("attached, %s speed", s_speed == 2 ? "high" : "full");
    unsigned last = s_flips, skipped = 0;
    if (recv_queue() < 0)
        return -1;
    for (;;) {
        u32 got = 0;
        SceUInt t = s_mode ? 20000 : 200000; /* casting: the sound goes every 20 ms at least */
        sceKernelWaitEventFlag(s_ev, EV_OUT_DONE | EV_DETACH | EV_QUIT | (s_mode ? EV_FLIP : 0), PSP_EVENT_WAITOR, &got, &t);
        if (got & (EV_DETACH | EV_QUIT) || !s_speed)
            return -1;
        if (got & EV_OUT_DONE) {
            sceKernelClearEventFlag(s_ev, ~EV_OUT_DONE);
            int n = s_out_req.retcode == 0 ? s_out_req.recvsize : -1;
            dcache_flush(s_cmd, sizeof(s_cmd));
            XltcHeader c;
            memcpy(&c, s_cmd, sizeof(c));
            if (recv_queue() < 0)
                return -1;
            if (n == sizeof(c) && c.magic == XLTC_MAGIC) {
                if (command(&c) < 0)
                    return -1;
            } else {
                LOG("command of %d bytes ignored", n);
            }
        }
        if (s_mode && s_flips != last) {
            sceKernelClearEventFlag(s_ev, ~EV_FLIP);
            last = s_flips;
            if (++skipped >= (unsigned)s_every) {
                skipped = 0;
                if (send_frame(last) < 0)
                    return -1;
            }
        }
        if (s_mode) { /* the sound mixed since the last time */
            const short *a;
            unsigned first;
            int n = audio_take(&a, &first);
            if (n > 0) {
                if (header(XLTC_AUDIO, 0, 0, n * 4, 44100, first, 0, 0) < 0 || send_from(a, n * 4) < 0)
                    return -1;
                s_st.audio_frames += n;
            }
        }
    }
}

static int cast_thread(SceSize args, void *argp)
{
    while (s_run) {
        if (!s_speed) {
            u32 got = 0;
            sceKernelWaitEventFlag(s_ev, EV_ATTACH | EV_QUIT, PSP_EVENT_WAITOR, &got, NULL);
            if (got & EV_QUIT)
                break;
        }
        sceKernelClearEventFlag(s_ev, ~(EV_ATTACH | EV_DETACH));
        if (!s_speed)
            continue;
        session();
        s_mode = 0;
        f_bd_cancel_all(&s_ep[1]);
        f_bd_cancel_all(&s_ep[2]);
        copy_free();
        audio_stop();
        LOG("session over (%s)", s_speed ? "still attached" : "detached");
        if (s_speed)
            sceKernelDelayThread(100000);
    }
    return 0;
}

/* --------------------------------------------------------------- watcher */

static volatile int s_watching;

static int watch_thread(SceSize args, void *argp)
{
    void *last = NULL;
    while (s_run && s_watching) {
        /* The wait must really wait: once the game quits and the display goes, it can
         * return at once, and a thread looping on it starves the quitting itself ("Please
         * wait" for ever, God of War on an E1004, 2026-11-20). Less than 4 ms (a frame is
         * 16.7): a frame's sleep instead. */
        u32 t0 = sceKernelGetSystemTimeLow();
        int w = f_vblank();
        if (w < 0 || sceKernelGetSystemTimeLow() - t0 < 4000)
            sceKernelDelayThread(16667);
        s_vblanks++;
        void *top = NULL;
        int stride = 0, fmt = 0;
        if (f_getbuf(&top, &stride, &fmt, 0) < 0)
            continue;
        s_stride = stride;
        s_fmt = fmt;
        if (top != last) {
            last = top;
            s_top = top;
            s_flips++;
            if (s_ev >= 0)
                sceKernelSetEventFlag(s_ev, EV_FLIP);
        }
    }
    return 0;
}

/* ------------------------------------------------------- driver start/stop */

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
    sceKernelClearEventFlag(s_ev, 0); /* EV_QUIT from the last USB off, among others */
    /* homebrew may never give the CPU up: there the sender has to come before it */
    int prio = s_homebrew ? 0x18 : 0x30;
    s_thread = sceKernelCreateThread("XLTCast", cast_thread, prio, 0x2000, 0, NULL);
    s_st.prio = prio;
    if (s_thread < 0 || sceKernelStartThread(s_thread, 0, NULL) < 0) {
        LOG("thread: %08x", (unsigned)s_thread);
        return -1;
    }
    return 0;
}

static int drv_stop(int size, void *p)
{
    if (s_thread >= 0) {
        sceKernelSetEventFlag(s_ev, EV_QUIT);
        SceUInt t = 1000000;
        if (sceKernelWaitThreadEnd(s_thread, &t) < 0)
            sceKernelTerminateThread(s_thread);
        sceKernelDeleteThread(s_thread);
        s_thread = -1;
    }
    return 0;
}

struct UsbDriver g_driver = {
    XLTCAST_DRIVER, 3, s_ep, &s_intp,
    NULL, NULL, NULL, NULL,
    (struct StringDescriptor *)s_str,
    cb_control, cb_func28, cb_attach, cb_detach,
    0, drv_start, drv_stop, NULL,
};

/* ------------------------------------------------------------- start-up */

static int s_registered;
static volatile int s_started;

/* Every thread ended before the module goes: one left asleep would wake up in freed
 * code, and the PSP hangs at "Please wait" when the game quits (God of War, E1004,
 * 2026-11-20: the button thread wasn't waited for). */
static void end_thread(SceUID *t, SceUInt us)
{
    if (*t < 0)
        return;
    SceUInt left = us;
    if (sceKernelWaitThreadEnd(*t, &left) < 0)
        sceKernelTerminateThread(*t);
    sceKernelDeleteThread(*t);
    *t = -1;
}

/* once: usb.prx, its functions, the driver registered (it stays registered, as XLT sync's) */
static int usb_prepare(void)
{
    if (s_registered)
        return 0;
    SceUID mid = sceKernelLoadModule("flash0:/kd/usb.prx", 0, NULL);
    int st = 0, r = mid >= 0 ? sceKernelStartModule(mid, 0, NULL, &st, NULL) : mid;
    LOG("usb.prx: load %08x start %08x", (unsigned)mid, (unsigned)r); /* 80020139: there already */

    f_usb_start = find_usb("sceUsb_driver", NID_USB_START);
    f_usb_stop = find_usb("sceUsb_driver", NID_USB_STOP);
    f_usb_activate = find_usb("sceUsb_driver", NID_USB_ACTIVATE);
    f_usb_deactivate = find_usb("sceUsb_driver", NID_USB_DEACT);
    f_bd_register = find_usb("sceUsbBus_driver", NID_BD_REGISTER);
    f_bd_unregister = find_usb("sceUsbBus_driver", NID_BD_UNREGISTER);
    f_bd_send = find_usb("sceUsbBus_driver", NID_BD_SEND);
    f_bd_recv = find_usb("sceUsbBus_driver", NID_BD_RECV);
    f_bd_cancel_all = find_usb("sceUsbBus_driver", NID_BD_CANCELALL);
    if (!f_usb_start || !f_usb_stop || !f_usb_activate || !f_usb_deactivate || !f_bd_register || !f_bd_unregister ||
        !f_bd_send || !f_bd_recv || !f_bd_cancel_all) {
        LOG("usb functions missing (module %s)", s_usb_mod ? s_usb_mod : "not found");
        return -1;
    }
    r = f_bd_register(&g_driver);
    LOG("sceUsbbdRegister %08x", (unsigned)r);
    if (r < 0)
        return -1;
    s_registered = 1;
    return 0;
}

/* USB on: the PSP shows up on the PC as XLT Cast */
/* Homebrew that never gives the CPU up starves the USB driver's own threads: transfers
 * never end (NFSU2VIEW on an E1004, 2026-11-20). While casting there, those threads (code
 * in usb.prx) run above it, and go back to their own priority after. */
#define USB_BOOST_PRIO 0x0C
static struct { SceUID id; int prio; } s_boost[16];
static int s_nboost;

static void usb_boost(int on)
{
    if (!on) {
        for (int i = 0; i < s_nboost; i++)
            sceKernelChangeThreadPriority(s_boost[i].id, s_boost[i].prio);
        s_nboost = 0;
        return;
    }
    SceModule *m = s_usb_mod ? sceKernelFindModuleByName(s_usb_mod) : NULL;
    if (!m) {
        LOG("USB threads: usb.prx not found");
        return;
    }
    static SceUID ids[96];
    int n = 0;
    if (sceKernelGetThreadmanIdList(SCE_KERNEL_TMID_Thread, ids, 96, &n) < 0)
        return;
    for (int i = 0; i < n && i < 96 && s_nboost < 16; i++) {
        SceKernelThreadInfo info;
        memset(&info, 0, sizeof(info));
        info.size = sizeof(info);
        if (sceKernelReferThreadStatus(ids[i], &info) < 0)
            continue;
        u32 e = (u32)info.entry;
        if (e < m->text_addr || e >= m->text_addr + m->text_size || info.currentPriority <= USB_BOOST_PRIO)
            continue;
        s_boost[s_nboost].id = ids[i];
        s_boost[s_nboost].prio = info.currentPriority;
        s_nboost++;
        sceKernelChangeThreadPriority(ids[i], USB_BOOST_PRIO);
        LOG("USB thread %s: priority 0x%x -> 0x%x", info.name, info.currentPriority, USB_BOOST_PRIO);
    }
    if (!s_nboost)
        LOG("USB threads: none found to raise");
}

static void usb_off(const char *why);

/* A program that quits by itself (homebrew's own Exit, not the HOME menu) quits with USB on,
 * and the PSP hangs at "Please wait": sceKernelExitGame is patched to turn it off first. */
#define NID_EXIT_GAME        0x05572A5F
#define NID_EXIT_GAME_STATUS 0x2AC9954B
static void (*o_exit)(void);
static void (*o_exit_status)(int);
static void h_exit(void) { usb_off("the program quits"); o_exit(); }
static void h_exit_status(int st) { usb_off("the program quits"); o_exit_status(st); }

static int exit_hooks_install(void)
{
    int k = 0;
    o_exit = find("sceLoadExec", "LoadExecForUser", NID_EXIT_GAME);
    if (o_exit) { sctrlHENPatchSyscall(o_exit, h_exit); k++; }
    o_exit_status = find("sceLoadExec", "LoadExecForUser", NID_EXIT_GAME_STATUS);
    if (o_exit_status) { sctrlHENPatchSyscall(o_exit_status, h_exit_status); k++; }
    return k;
}

static int usb_on(void)
{
    if (s_started)
        return 0;
    if (usb_prepare() < 0)
        return -1;
    /* the watcher counts the game's frames only while casting: outside it, a game has
     * nothing of xltcast running but the button thread */
    if (s_watch < 0) {
        s_watching = 1;
        /* homebrew that never gives the CPU up starved it at 0x20 (NFSU2VIEW: 0 of 60 vblanks seen,
         * 0.6 fps): there it runs above the sender. Safe: it sleeps a frame if a wait comes back at once */
        s_watch = sceKernelCreateThread("XLTCastWatch", watch_thread, s_homebrew ? 0x16 : 0x20, 0x1000, 0, NULL);
        if (s_watch < 0 || sceKernelStartThread(s_watch, 0, NULL) < 0) {
            LOG("watcher: %08x", (unsigned)s_watch);
            if (s_watch >= 0)
                sceKernelDeleteThread(s_watch);
            s_watch = -1;
            return -1;
        }
    }
    int r1 = f_usb_start(PSP_USBBUS_DRIVERNAME, 0, 0);
    int r2 = f_usb_start(XLTCAST_DRIVER, 0, 0);
    int r3 = r2 < 0 ? r2 : f_usb_activate(XLTCAST_PID);
    LOG("USB on: bus %08x cast %08x, activate %08x", (unsigned)r1, (unsigned)r2, (unsigned)r3);
    if (r2 < 0 || r3 < 0) {
        if (r2 >= 0)
            f_usb_stop(XLTCAST_DRIVER, 0, 0);
        f_usb_stop(PSP_USBBUS_DRIVERNAME, 0, 0);
        return -1;
    }
    s_started = 1;
    if (s_homebrew)
        usb_boost(1);
    return 0;
}

/* USB off, as XLT does before it leaves: still running when the game quits, the
 * PSP hangs at "Please wait" (God of War: Ghost of Sparta, E1004, 2026-11-20) */
static void usb_off(const char *why)
{
    if (!s_started)
        return;
    f_usb_deactivate(XLTCAST_PID);
    f_usb_stop(XLTCAST_DRIVER, 0, 0); /* drv_stop: the sending thread ends */
    f_usb_stop(PSP_USBBUS_DRIVERNAME, 0, 0);
    s_started = 0;
    s_mode = 0;
    copy_free();
    audio_stop();
    s_watching = 0;
    end_thread(&s_watch, 100000);
    usb_boost(0);
    LOG("USB off (%s)", why);
}

#define NID_CTRL_PEEK      0x3A622550 /* sceCtrlPeekBufferPositive */
#define NID_CTRL_INTERCEPT 0x7CA723DC /* sceCtrlSetButtonMasks */
#define COMBO (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)

static int starter_thread(SceSize args, void *argp)
{
    sceKernelDelayThread(START_DELAY_US);
    if (!s_run)
        return 0;
    /* ARK's "game" runlevel takes in homebrew too, XLT among them: XLT has its own USB
     * (sync), so nothing is started there (as xltshot) */
    if (sceKernelFindModuleByName("XLT")) {
        LOG("XLT is running: keeping out of its way");
        return 0;
    }
    if (s_skip) {
        LOG("XLT or ARK's launcher: keeping out of its way");
        return 0;
    }
    int (*peek)(SceCtrlData *, int) = find("sceController_Service", "sceCtrl_driver", NID_CTRL_PEEK);
    u32 (*intercept)(u32, u32) = find("sceController_Service", "sceCtrl_driver", NID_CTRL_INTERCEPT);
    s_ev = sceKernelCreateEventFlag("XLTCastEv", 0, 0, NULL);
    const char *file = sceKernelInitFileName();
    LOG("%s: %s; L+R+SELECT turns casting on (buttons %s)", s_homebrew ? "homebrew" : "game", file ? file : "?",
        peek ? "ok" : "MISSING");
    if (s_ev < 0 || !peek)
        return 0;

    /* (colour depth: logged while casting, when the watcher runs) */
    /* Casting is off until L+R+SELECT (SELECT hidden from the game while L+R are held,
     * as xltshot does with START); the same again turns it off, and so does HOME, at
     * once: quitting is chosen from the HOME menu, and USB must be off by then. */
    static const char *names[] = {"565 (16-bit)", "5551 (16-bit)", "4444 (16-bit)", "8888 (32-bit)"};
    int held = 0, masked = 0, logged = -1, n = 0;
    u32 t0 = sceKernelGetSystemTimeLow(), last_colour = 0;
    while (s_run) {
        SceCtrlData pad;
        memset(&pad, 0, sizeof(pad));
        if (peek(&pad, 1) >= 0) {
            if (pad.Buttons & PSP_CTRL_HOME) {
                usb_off("HOME");
            } else {
                int lr = (pad.Buttons & COMBO) == COMBO;
                if (intercept && lr != masked) {
                    intercept(PSP_CTRL_SELECT, lr ? 1 : 0);
                    masked = lr;
                }
                int combo = lr && (pad.Buttons & PSP_CTRL_SELECT);
                if (combo && !held) {
                    if (s_started)
                        usb_off("L+R+SELECT");
                    else if (usb_on() < 0)
                        LOG("USB didn't start");
                }
                held = combo;
            }
        }
        /* the game's colour depth, for choosing test games: changes in its first two minutes */
        u32 now = sceKernelGetSystemTimeLow();
        if (n < 6 && now - t0 < 120000000u && now - last_colour > 2000000u) {
            last_colour = now;
            int fmt = s_fmt;
            if (s_flips && fmt != logged) {
                LOG("colour: %s, buffer %d wide", fmt >= 0 && fmt < 4 ? names[fmt] : "?", (int)s_stride);
                logged = fmt;
                n++;
            }
        }
        sceKernelDelayThread(20000);
    }
    if (masked && intercept)
        intercept(PSP_CTRL_SELECT, 0);
    return 0;
}

int module_start(SceSize args, void *argp)
{
    SceUID d = sceIoDopen("ms0:/");
    if (d >= 0) sceIoDclose(d);
    else snprintf(s_dev, sizeof(s_dev), "ef0:");
    f_getbuf = find("sceDisplay_Service", "sceDisplay_driver", NID_DISP_GETBUF);
    f_vblank = find("sceDisplay_Service", "sceDisplay_driver", NID_DISP_VBLANK);
    f_ge_drawsync = find("sceGE_Manager", "sceGe_driver", NID_GE_DRAWSYNC);
    if (!f_ge_drawsync)
        f_ge_drawsync = find("sceGE_Manager", "sceGe_user", NID_GE_DRAWSYNC);
    char l[160];
    snprintf(l, sizeof(l), "-- xltcast %d, apitype %x: frame buffer %s, vblank %s", XLTC_VERSION,
             sceKernelInitApitype(), f_getbuf ? "ok" : "MISSING", f_vblank ? "ok" : "MISSING");
    /* kept across loads (a game, then XLT, each load it), started over past 16 KB */
    char lp[64];
    snprintf(lp, sizeof(lp), "%sSEPLUGINS/xltcast.log", s_dev);
    SceIoStat st;
    log_line(l, sceIoGetstat(lp, &st) >= 0 && st.st_size > 16 * 1024);
    if (!f_getbuf || !f_vblank)
        return 0;
    /* XLT (and ARK's launcher, which is XLT's copy) has its own USB: nothing at all there.
     * Its module isn't loaded yet, so it is told by its path. */
    int api = sceKernelInitApitype();
    s_homebrew = api == 0x141 || api == 0x152;
    const char *file = sceKernelInitFileName();
    s_skip = file && (strstr(file, "/PSP/GAME/XLT/") || strstr(file, "ARK_01234"));
    if (s_skip)
        return 0;
    /* the sound: the channels' set-up is noted from the start, as programs reserve them
     * while they boot; and the exit calls, so that USB is off before a program quits */
    int k = audio_hooks_install(), x = exit_hooks_install();
    char a[80];
    snprintf(a, sizeof(a), "sound: %d of 15 calls hooked; exit: %d of 2", k, x);
    log_line(a, 0);
    build_ms_props();
    build_msos20();
    s_starter = sceKernelCreateThread("XLTCastStart", starter_thread, 0x18, 0x1000, 0, NULL);
    if (s_starter >= 0)
        sceKernelStartThread(s_starter, 0, NULL);
    return 0;
}

int module_stop(SceSize args, void *argp)
{
    LOG("stopping (USB %s)", s_started ? "on" : "off");
    s_run = 0;
    if (s_ev >= 0)
        sceKernelSetEventFlag(s_ev, EV_QUIT);
    end_thread(&s_starter, 500000); /* polls every 20 ms; may be in its first 4 s of waiting */
    if (s_started) {
        f_usb_deactivate(XLTCAST_PID);
        f_usb_stop(XLTCAST_DRIVER, 0, 0); /* drv_stop ends the sending thread */
        f_usb_stop(PSP_USBBUS_DRIVERNAME, 0, 0);
        s_started = 0;
    }
    end_thread(&s_thread, 500000);
    if (s_registered)
        f_bd_unregister(&g_driver);
    end_thread(&s_watch, 100000); /* wakes at every vertical blank */
    if (s_ev >= 0)
        sceKernelDeleteEventFlag(s_ev);
    if (s_copy_block >= 0)
        sceKernelFreePartitionMemory(s_copy_block);
    LOG("stopped");
    return 0;
}
