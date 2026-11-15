#ifndef HOSTIO_PSPIOFILEMGR_H
#define HOSTIO_PSPIOFILEMGR_H
/* Host stand-ins that really read and write (tests/test_history.c): "ms0:/" and
 * "ef0:/" are folders under $HOSTIO_ROOT. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
typedef int SceUID;
#define PSP_O_RDONLY 1
#define PSP_O_WRONLY 2
#define PSP_O_RDWR 3
#define PSP_O_CREAT 0x200
#define PSP_O_TRUNC 0x400
#define PSP_O_APPEND 0x100
#define PSP_SEEK_SET 0
#define PSP_SEEK_END 2
static inline const char *hostio_path(const char *p, char *buf, int n)
{
    snprintf(buf, n, "%s/%.3s/%s", getenv("HOSTIO_ROOT"), p, p + 5);
    return buf;
}
static inline SceUID sceIoOpen(const char *p, int f, int m)
{
    char b[512];
    int fl = (f & 3) == 1 ? O_RDONLY : (f & 3) == 2 ? O_WRONLY : O_RDWR;
    if (f & PSP_O_CREAT) fl |= O_CREAT;
    if (f & PSP_O_TRUNC) fl |= O_TRUNC;
    if (f & PSP_O_APPEND) fl |= O_APPEND;
    return open(hostio_path(p, b, sizeof(b)), fl, m);
}
static inline int sceIoRead(SceUID fd, void *b, unsigned n) { return (int)read(fd, b, n); }
static inline int sceIoWrite(SceUID fd, const void *b, unsigned n) { return (int)write(fd, b, n); }
static inline int sceIoClose(SceUID fd) { return close(fd); }
static inline int sceIoLseek32(SceUID fd, int off, int whence) { return (int)lseek(fd, off, whence == 2 ? SEEK_END : SEEK_SET); }
static inline int sceIoRemove(const char *p) { char b[512]; return unlink(hostio_path(p, b, sizeof(b))); }
#endif
