/* XLT - XLT Cast: the in-game plugin (plugin/xltcast) that shows the game on the PC,
 * carried inside XLT and installed by it. Settings > XLT > XLT Cast (g_cfg.cast).
 *
 * On: SEPLUGINS/xltcast.prx is written if it is missing or isn't the one this XLT
 * carries, and its PLUGINS.TXT lines are switched on, or "game, ms0:/SEPLUGINS/
 * xltcast.prx, on" is added if no list has one. Off: its lines are switched off (the
 * file stays). Per-game lines are the user's and are left alone. Done at every start
 * (an XLT update brings a new plugin) and when the setting changes, on the background
 * worker; it applies from the next game.
 *
 * In a game, casting stays off until L+R+SELECT; L+R+SELECT again or HOME turns it off. */
#ifndef CASTPLUG_H
#define CASTPLUG_H

void castplug_apply(int on);

#endif
