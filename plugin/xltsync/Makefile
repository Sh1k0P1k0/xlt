# xltsync: XLT's USB link to the PC (a kernel PRX XLT loads while sync is on).
# XLT's own Makefile builds it and embeds it.
TARGET = xltsync
OBJS = main.o syncpath.o fatcount.o
PRX_EXPORTS = exports.exp

BUILD_PRX = 1
PSP_FW_VERSION = 660
USE_KERNEL_LIBS = 1

CFLAGS = -Os -G0 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-format-truncation
ASFLAGS = $(CFLAGS)
# the 6.60 versions of the kernel libraries (their function IDs changed in 6.60), ahead of build.mak's -lpspkernel
LIBS = -lpspusbbus_driver -lpspsystemctrl_kernel -lpspsysclib_kernel_660 -lpspthreadman_kernel_660 -lpspsysmem_kernel_660

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

main.o: main.c xltsync.h syncpath.h ../../src/fatcount.h
syncpath.o: syncpath.c syncpath.h
# free space from the FAT: XLT's own counter (src/fatcount.c), built here (after build.mak: the first rule is the default goal) for the kernel
fatcount.o: ../../src/fatcount.c ../../src/fatcount.h
	$(CC) $(CFLAGS) -c -o $@ $<
