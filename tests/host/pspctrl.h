#ifndef HOST_PSPCTRL_H
#define HOST_PSPCTRL_H
/* The button bits, as in the SDK (tests/web_preview.c presses them). */
enum {
    PSP_CTRL_SELECT = 0x000001, PSP_CTRL_START = 0x000008, PSP_CTRL_UP = 0x000010, PSP_CTRL_RIGHT = 0x000020,
    PSP_CTRL_DOWN = 0x000040, PSP_CTRL_LEFT = 0x000080, PSP_CTRL_LTRIGGER = 0x000100, PSP_CTRL_RTRIGGER = 0x000200,
    PSP_CTRL_TRIANGLE = 0x001000, PSP_CTRL_CIRCLE = 0x002000, PSP_CTRL_CROSS = 0x004000, PSP_CTRL_SQUARE = 0x008000,
    PSP_CTRL_NOTE = 0x800000, PSP_CTRL_VOLUP = 0x100000, PSP_CTRL_VOLDOWN = 0x200000,
    PSP_CTRL_HOME = 0x010000, PSP_CTRL_HOLD = 0x020000, PSP_CTRL_WLAN_UP = 0x040000, PSP_CTRL_REMOTE = 0x080000,
    PSP_CTRL_SCREEN = 0x400000, PSP_CTRL_DISC = 0x1000000, PSP_CTRL_MS = 0x2000000,
};
/* The pad as sceCtrlPeekBufferPositive fills it (tests/btntest_preview.c). */
typedef struct { unsigned TimeStamp, Buttons; unsigned char Lx, Ly, Rsrv[6]; } SceCtrlData;
int sceCtrlPeekBufferPositive(SceCtrlData *pad, int count);
#endif
