/* XLT - USB sync (plugin/xltsync): with the cable in, the PC sends, renames and
 * deletes files while XLT runs. Settings > XLT > USB sync (g_cfg.usb_sync)
 * keeps it on from start to exit; it and the USB mass storage mode share the
 * port, so the storage mode pauses it (sysctl.c, cc.c) and it resumes after. */
#ifndef USBSYNC_H
#define USBSYNC_H

void usbsync_init(const char *dir); /* XLT's folder: the module and its log go there */
int usbsync_start(void);            /* 0 on success; usbsync_error() says why not */
void usbsync_stop(void);
int usbsync_active(void);           /* started */
int usbsync_attached(void);         /* the cable is in and the PC set the device up */
int usbsync_talking(void);          /* a PC program is connected */
const char *usbsync_error(void);

/* Every frame from the main loop: reads the module's state a few times a
 * second, rescans the library once the PC's changes have settled, nudges a
 * connection that doesn't come up. 1 if anything shown changed. */
int usbsync_poll(void);
/* "From PC 45%" while a file comes in: 1 and the text, else 0 */
int usbsync_progress(char *out, int n);
void usbsync_on_resume(void);       /* from the power callback: restarted after sleep */

#endif
