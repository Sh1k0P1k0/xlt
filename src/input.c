/* XLT - pad input with d-pad auto-repeat. The analog stick acts as a d-pad. */
#include "input.h"

#define DPAD (PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_RIGHT)

static unsigned int s_prev;
static int s_hold_frames;

void input_init(void)
{
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
}

void input_poll(Input *in)
{
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad, 1);
    unsigned int now = pad.Buttons;

    if (pad.Lx < 40) now |= PSP_CTRL_LEFT;
    if (pad.Lx > 215) now |= PSP_CTRL_RIGHT;
    if (pad.Ly < 40) now |= PSP_CTRL_UP;
    if (pad.Ly > 215) now |= PSP_CTRL_DOWN;

    in->held = now;
    in->pressed = now & ~s_prev;

    unsigned int dir = now & DPAD;
    if (dir && dir == (s_prev & DPAD)) {
        s_hold_frames++;
        if (input_repeats(s_hold_frames))
            in->pressed |= dir;
    } else {
        s_hold_frames = 0;
    }
    s_prev = now;
}
