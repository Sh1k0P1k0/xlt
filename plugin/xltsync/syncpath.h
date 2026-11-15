/* xltsync - which paths the PC may read and change (pure: no PSP calls, so
 * tests/test_syncpath.c checks it on the PC). */
#ifndef XLTSYNC_SYNCPATH_H
#define XLTSYNC_SYNCPATH_H

enum { P_READ, P_CHANGE, P_MKDIR };

/* Is path one the PC may use this way? "ms0:/" or "ef0:/", then names separated
 * by single slashes: no "." or "..", nothing FAT can't hold, no control
 * characters, valid Shift-JIS.
 *   P_READ:   anywhere (the device itself too), and flash0:/font.
 *   P_CHANGE: inside one of XLT's folders (ISO, VIDEO, MUSIC, PICTURE, BOOKS,
 *             DOWNLOADS, PSP/GAME), not the folder itself; or in PSP/SAVEDATA,
 *             a save folder or a file directly in one, nothing deeper.
 *   P_MKDIR:  as P_CHANGE, and the folders themselves too. */
int path_ok(const char *p, int mode);

/* PSP/SAVEDATA/...: 1 for a save folder, 2 for a file in one, 0 for anything else. */
int path_save_depth(const char *p);

/* 1 if `inner` is `outer` itself or inside it (case-insensitive, as FAT). */
int path_within(const char *inner, const char *outer);

#endif
