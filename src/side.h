/* XLT - the nuMB side project's own build (docs/numb.md, step N0).
 *
 * Built with make SIDE=1 (the default on the numb branch; plain make builds the real XLT
 * since nuMB was merged), XLT becomes "XLT nuMB", a test copy. It is
 * installed in PSP/GAME/NUMB/ beside the real XLT and must never change anything the
 * real XLT owns or depends on:
 * - Its own folder. xlt.ini, cache/, the sync module and logs all live in PSP/GAME/NUMB/.
 * - No plugins installed or switched. xltshot and xltcast stay as the real XLT set them,
 *   and their PLUGINS.TXT lines too.
 * - Play time stays XLT's. SEPLUGINS/xltplay.bin is read and then deleted by the real XLT,
 *   so nuMB doesn't import it.
 * - ARK's launcher isn't touched. "Start XLT at boot" is hidden, and nothing pauses it on exit.
 * - No self-update. The update would replace nuMB with a real XLT release, and also ARK's
 *   VBOOT.PBP.
 * The module name stays "XLT", so xltshot and xltcast still recognise it and keep out of
 * its way. Settings the user changes on purpose (ARK's options, plugins, system settings)
 * still work, as they do from the XMB.
 *
 * Built with SIDE=0 it is the normal XLT, unchanged. */
#ifndef SIDE_H
#define SIDE_H

#ifdef XLT_SIDE
#define XLT_SIDE_BUILD 1
#define XLT_HOME_DIR "ms0:/PSP/GAME/NUMB/"
#else
#define XLT_SIDE_BUILD 0
#define XLT_HOME_DIR "ms0:/PSP/GAME/XLT/"
#endif

#endif
