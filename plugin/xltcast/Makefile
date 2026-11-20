# xltcast: the game's picture on the PC over USB (XLT Cast), speed test version
# (roadmap step C1). A kernel PRX ARK loads at the "game" runlevel; not yet built
# into or installed by XLT: copy xltcast.prx to SEPLUGINS and add it to PLUGINS.TXT.
TARGET = xltcast
OBJS = main.o audio.o

BUILD_PRX = 1
PSP_FW_VERSION = 660
USE_KERNEL_LIBS = 1

CFLAGS = -O2 -G0 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
ASFLAGS = $(CFLAGS)
# the 6.60 versions of the kernel libraries (their function IDs changed in 6.60), ahead of build.mak's -lpspkernel;
# the USB and display functions are looked up at run time (usb.prx isn't loaded in most games)
LIBS = -lpspsystemctrl_kernel -lpspsysclib_kernel_660 -lpspthreadman_kernel_660 -lpspsysmem_kernel_660 \
       -lpspmodulemgr_kernel_660 -lpsploadcore_kernel_660 -lpspinit_kernel_660 -lgcc

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

main.o: main.c xltcast.h audio.h
audio.o: audio.c audio.h
