/* XLT - Internet over USB with USBNet, by chriopter (github.com/chriopter/pspkit-usbnet, MIT):
 * the PSP's network over the USB cable, with the PC as its router (XLT Tool runs USBNet's
 * gateway). XLT doesn't carry USBNet. Settings > Network > Internet over USB (g_cfg.usb_net).
 *
 * Installed already -- seplugins/usbnet/usbnet.prx (PSPDX's place) or seplugins/usbnet.prx (the
 * README's) -- it is left as it is: no file of it is written. Not installed, and XLT Tool has
 * left USBNet's release in PSP/GAME/XLT/USBNET/ (its sync may not write SEPLUGINS), it goes
 * where PSPDX puts it, seplugins/usbnet/ with every file of the release, and the staged copy
 * is removed. XLT writes no PSPDX record.
 *
 * The line: the last line of any list that names the installed file is switched on or off
 * with the setting, in place the way PSPDX switches its own ("off" <-> "on "); if none names
 * it, PSPDX's line "always, ms0:/seplugins/usbnet/usbnet.prx, on " is added to
 * seplugins/PLUGINS.TXT. XLT's own former plugin's lines (xltnet.prx) are switched off.
 * Done at every start and when the setting changes, on the background worker; it applies
 * from the next start of a game, XLT, or the XMB. */
#ifndef NETPLUG_H
#define NETPLUG_H

void netplug_apply(int on);

#endif
