/* XLT - the crash log: the plugin (plugin/xltcrash) that writes SEPLUGINS/xltcrash.log
 * when a game, an app, a plugin or XLT itself crashes, carried inside XLT and installed
 * by it. Settings > XLT > Crash log (g_cfg.crash_log, on by default).
 *
 * On: SEPLUGINS/xltcrash.prx is written if it is missing or isn't the one this XLT
 * carries, and its PLUGINS.TXT lines are switched on, or "game, .../xltcrash.prx, on"
 * and "pops, .../xltcrash.prx, on" are added if no list has one. Off: its lines are
 * switched off (the file stays). Done at every start and when the setting changes, on
 * the background worker; it applies from the next game (or XLT's next start). */
#ifndef CRASHPLUG_H
#define CRASHPLUG_H

void crashplug_apply(int on);

#endif
