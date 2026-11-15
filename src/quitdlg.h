/* XLT - its own HOME dialog, in place of the system's.
 *
 * While XLT runs, the system's HOME dialog is off (sysctl_home_popup) and HOME
 * opens this one instead: "Quit XLT?" Yes / No, as the system asks. Yes leaves
 * the way the system's Yes did (XLT's normal exit to the XMB), No closes it.
 *
 * The main loop keeps the system dialog off and tells the watchdog it is alive
 * every frame; if XLT stops answering for 3 s the watchdog turns the system
 * dialog back on, so HOME can always quit a stuck XLT. It is turned back on
 * before a game or app starts and when XLT ends. */
#ifndef QUITDLG_H
#define QUITDLG_H

#include "input.h"

/* Every frame, first thing after input_poll(): keeps the system dialog off and
 * adds a HOME press (read through the kernel) to in->pressed, so the boot
 * screen and the screensaver take it like any button. */
void quitdlg_poll(Input *in);
/* After the screensaver and the button test: a HOME press opens the dialog;
 * while it is up it takes every press. 1 when it needs a redraw. */
int quitdlg_update(Input *in);
int quitdlg_is_open(void);
void quitdlg_draw(void);
/* Before XLT hands the PSP to a game, an app or the XMB: the system dialog back on. */
void quitdlg_release(void);

#endif
