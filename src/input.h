#ifndef INPUT_H
#define INPUT_H

#include <pspctrl.h>

#define REPEAT_DELAY 18 /* frames a button is held before it repeats */
#define REPEAT_RATE 4   /* frames between repeats */

/* 1 on the frames a button held for `frames` frames repeats. */
static inline int input_repeats(int frames)
{
    return frames >= REPEAT_DELAY && (frames - REPEAT_DELAY) % REPEAT_RATE == 0;
}

typedef struct {
    unsigned int held;
    unsigned int pressed; /* new presses, plus auto-repeat for the d-pad */
} Input;

void input_init(void);
void input_poll(Input *in);

#endif
