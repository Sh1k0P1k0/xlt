/* XLT - Settings > About > Check for updates.
 *
 * Asks GitHub for the latest XLT release over Wi-Fi (the connection used last),
 * and on the user's say-so downloads its zip, checks it against GitHub's SHA-256,
 * unpacks the EBOOT.PBP next to XLT's own, keeps the running one as
 * EBOOT-<version>.PBP and puts the new one in its place (and in ARK's VBOOT.PBP,
 * when XLT starts at boot). The new XLT runs from the next start.
 *
 * The network part runs on webfetch.c's network thread, the install on the
 * background worker; the UI calls update_step() once a frame. */
#ifndef UPDATE_H
#define UPDATE_H

#include <stdint.h>
#include "updparse.h"

enum {
    UP_IDLE,
    UP_CHECKING,    /* asking GitHub */
    UP_LATEST,      /* nothing newer */
    UP_AVAILABLE,   /* a newer one, update_release() says which */
    UP_DOWNLOADING,
    UP_INSTALLING,
    UP_INSTALLED,   /* restart to use it */
    UP_FAILED,      /* update_error() says why */
};

void update_init(const char *dir);   /* XLT's folder, ending in '/' */

void update_check(void);             /* UP_CHECKING; ignored while busy */
void update_install(void);           /* from UP_AVAILABLE: UP_DOWNLOADING, then UP_INSTALLING */
void update_stop(void);              /* stops a check or a download (not an install) */
void update_dismiss(void);           /* the dialog is closed: back to UP_IDLE, Wi-Fi dropped */
int update_step(void);               /* once a frame: 1 if the state changed */

int update_state(void);
int update_busy(void);               /* checking, downloading or installing */
const UpdRelease *update_release(void);  /* the release found (UP_AVAILABLE on) */
const char *update_notes(void);      /* its notes as plain text */
const char *update_error(void);
const char *update_activity(void);   /* what a check or an install is doing right now */
void update_progress(int64_t *got, int64_t *total, int *kbps);
const char *update_backup(void);     /* UP_INSTALLED: the name the old XLT was kept under */
/* What was found the last time (for the About row): 0 nothing yet, 1 up to date,
 * 2 newer (version in update_release()), 3 installed (restart). */
int update_known(void);

void update_request_restart(void);   /* the dialog's Restart */
int update_take_restart(void);       /* main loop: 1 once after a request */

/* From XLT Tool, over USB sync: the Tool leaves the new XLT in UPDATE/ in XLT's folder
 * (XLT.PBP, and RELEASE.TXT: its version on the first line, then its notes), XLT.PBP last.
 * update_usb_look() (at start, and when the PC's changes have settled) finds it and makes
 * it the offer: UP_AVAILABLE with update_from_usb() 1, so update_install() installs it
 * as it would GitHub's, without Wi-Fi. Any version is taken, the same or an older one
 * too (the Tool says what it sends). Installed or failed, UPDATE/ is removed; "Later"
 * leaves it for the next time.
 * For the Tool, cache/version.txt says the running version ("1.3.1"), and after an
 * install a second line "next 1.4.0". */
int update_usb_look(void);           /* 1 when a new offer came in since the last look */
int update_from_usb(void);           /* the offer (or install) is the Tool's */

#endif
