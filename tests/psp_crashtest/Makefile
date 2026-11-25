# XLTCRASH: crashes on purpose, to check the crash log (see main.c).
# make, then copy EBOOT.PBP to PSP/GAME/XLTCRASH/.
TARGET = xltcrashtest
OBJS = main.o
CFLAGS = -O2 -G0 -Wall -Wno-array-bounds -fno-delete-null-pointer-checks -fno-isolate-erroneous-paths-dereference # the bad addresses are the point
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = XLT crash test
BUILD_PRX = 1
PSP_FW_VERSION = 660
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
